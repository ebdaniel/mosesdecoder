/*
 * PhraseTableMemory.cpp
 *
 *  Created on: 28 Oct 2015
 *      Author: hieu
 */

#include <cassert>
#include <boost/foreach.hpp>
#include "PhraseTableMemory.h"
#include "Node.h"
#include "../../PhraseImpl.h"
#include "../../Phrase.h"
#include "../../System.h"
#include "../../Scores.h"
#include "../../InputPathsBase.h"
#include "../../legacy/InputFileStream.h"
#include "util/exception.hh"

#include "../../PhraseBased/InputPath.h"
#include "../../PhraseBased/TargetPhraseImpl.h"
#include "../../PhraseBased/TargetPhrases.h"

#include "../../SCFG/PhraseImpl.h"
#include "../../SCFG/TargetPhraseImpl.h"
#include "../../SCFG/InputPath.h"
#include "../../SCFG/Stack.h"
#include "../../SCFG/Stacks.h"
#include "../../SCFG/Manager.h"


using namespace std;

namespace Moses2
{


////////////////////////////////////////////////////////////////////////

PhraseTableMemory::PhraseTableMemory(size_t startInd, const std::string &line) :
    PhraseTable(startInd, line)
{
  ReadParameters();
}

PhraseTableMemory::~PhraseTableMemory()
{
  // TODO Auto-generated destructor stub
}

void PhraseTableMemory::Load(System &system)
{
  FactorCollection &vocab = system.GetVocab();

  MemPool &systemPool = system.GetSystemPool();
  MemPool tmpSourcePool;
  vector<string> toks;
  size_t lineNum = 0;
  InputFileStream strme(m_path);
  string line;
  while (getline(strme, line)) {
    if (++lineNum % 1000000 == 0) {
      cerr << lineNum << " ";
    }
    toks.clear();
    TokenizeMultiCharSeparator(toks, line, "|||");
    UTIL_THROW_IF2(toks.size() < 3, "Wrong format");
    //cerr << "line=" << line << endl;

    Phrase *source;
    TargetPhrase *target;

    switch (system.options.search.algo) {
    case Normal:
    case CubePruning:
    case CubePruningPerMiniStack:
    case CubePruningPerBitmap:
    case CubePruningCardinalStack:
    case CubePruningBitmapStack:
    case CubePruningMiniStack:
      source = PhraseImpl::CreateFromString(tmpSourcePool, vocab, system,
          toks[0]);
      //cerr << "created soure" << endl;
      target = TargetPhraseImpl::CreateFromString(systemPool, *this, system,
          toks[1]);
      //cerr << "created target" << endl;
      break;
    case CYKPlus:
      source = SCFG::PhraseImpl::CreateFromString(tmpSourcePool, vocab, system,
          toks[0]);
      //cerr << "created soure" << endl;
      SCFG::TargetPhraseImpl *targetSCFG;
      targetSCFG = SCFG::TargetPhraseImpl::CreateFromString(systemPool, *this,
          system, toks[1]);
      targetSCFG->SetAlignmentInfo(toks[3]);
      target = targetSCFG;
      cerr << "created target " << *targetSCFG << endl;
      break;
    default:
      abort();
      break;
    }

    target->GetScores().CreateFromString(toks[2], *this, system, true);
    //cerr << "created scores:" << *target << endl;

    // properties
    if (toks.size() == 7) {
      //target->properties = (char*) system.systemPool.Allocate(toks[6].size() + 1);
      //strcpy(target->properties, toks[6].c_str());
    }

    system.featureFunctions.EvaluateInIsolation(systemPool, system, *source,
        *target);
    //cerr << "EvaluateInIsolation:" << *target << endl;
    m_root.AddRule(*source, target);
  }

  m_root.SortAndPrune(m_tableLimit, systemPool, system);
  cerr << "root=" << &m_root << endl;

  BOOST_FOREACH(const Node::Children::value_type &valPair, m_root.GetChildren()) {
    const Word &word = valPair.first;
    cerr << word << " ";
  }
  cerr << endl;
}

TargetPhrases* PhraseTableMemory::Lookup(const Manager &mgr, MemPool &pool,
    InputPathBase &inputPath) const
{
  const SubPhrase &phrase = inputPath.subPhrase;
  TargetPhrases *tps = m_root.Find(phrase);
  return tps;
}

void PhraseTableMemory::InitActiveChart(SCFG::InputPath &path) const
{
  size_t ptInd = GetPtInd();
  ActiveChartEntryMem *chartEntry = new ActiveChartEntryMem(NULL, false, &m_root);
  path.AddActiveChartEntry(ptInd, chartEntry);
}

void PhraseTableMemory::Lookup(MemPool &pool,
    const SCFG::Manager &mgr,
    const SCFG::Stacks &stacks,
    SCFG::InputPath &path) const
{
  size_t endPos = path.range.GetEndPos();

  // TERMINAL
  const Word &lastWord = path.subPhrase.Back();
  //cerr << "PhraseTableMemory lastWord=" << lastWord << endl;
  //cerr << "path=" << path << endl;
  const SCFG::InputPath &subPhrasePath = *mgr.GetInputPaths().GetMatrix().GetValue(endPos, 1);

  const SCFG::InputPath *prefixPath = static_cast<const SCFG::InputPath*>(path.prefixPath);
  UTIL_THROW_IF2(prefixPath == NULL, "prefixPath == NULL");
  LookupGivenPrefixPath(*prefixPath, lastWord, subPhrasePath, false, path);

  // NON-TERMINAL
  //const SCFG::InputPath *prefixPath = static_cast<const SCFG::InputPath*>(path.prefixPath);
  while (prefixPath) {
    const Range &prefixRange = prefixPath->range;
    //cerr << "prefixRange=" << prefixRange << endl;
    size_t startPos = prefixRange.GetEndPos() + 1;
    size_t ntSize = endPos - startPos + 1;
    const SCFG::InputPath &subPhrasePath = *mgr.GetInputPaths().GetMatrix().GetValue(startPos, ntSize);

    const SCFG::Stack &ntStack = stacks.GetStack(startPos, ntSize);
    const SCFG::Stack::Coll &coll = ntStack.GetColl();

    BOOST_FOREACH (const SCFG::Stack::Coll::value_type &valPair, coll) {
      const SCFG::Word &ntSought = valPair.first;
      //cerr << "ntSought=" << ntSought << ntSought.isNonTerminal << endl;
      LookupGivenPrefixPath(*prefixPath, ntSought, subPhrasePath, true, path);
    }

    prefixPath = static_cast<const SCFG::InputPath*>(prefixPath->prefixPath);
  }
}

void PhraseTableMemory::LookupGivenPrefixPath(const SCFG::InputPath &prefixPath,
    const Word &wordSought,
    const SCFG::InputPath &subPhrasePath,
    bool isNT,
    SCFG::InputPath &path) const
{
  size_t ptInd = GetPtInd();

  cerr << "prefixPath=" << prefixPath.range << " " << prefixPath.GetActiveChart(ptInd).entries.size() << endl;

  BOOST_FOREACH(const SCFG::ActiveChartEntry *entry, prefixPath.GetActiveChart(ptInd).entries) {
    const ActiveChartEntryMem *entryCast = static_cast<const ActiveChartEntryMem*>(entry);
    const Node *node = entryCast->node;
    UTIL_THROW_IF2(node == NULL, "node == NULL");
    cerr << "node=" << node << endl;

    LookupGivenNode(*node, wordSought, subPhrasePath, isNT, path);
  }
}

void PhraseTableMemory::LookupGivenNode(const Node &node,
    const Word &wordSought,
    const SCFG::InputPath &subPhrasePath,
    bool isNT,
    SCFG::InputPath &path) const
{
  size_t ptInd = GetPtInd();
  const Node *nextNode = node.Find(wordSought);
  cerr << "wordSought=" << wordSought << " " << nextNode << endl;

  if (nextNode) {
    // new entries
    ActiveChartEntryMem *chartEntry = new ActiveChartEntryMem(&subPhrasePath, isNT, nextNode);
    path.AddActiveChartEntry(ptInd, chartEntry);

    const SCFG::SymbolBind &symbolBind = chartEntry->symbolBinds;

    // there are some rules
    AddTargetPhrasesToPath(*nextNode, symbolBind, path);
  }

}

void PhraseTableMemory::AddTargetPhrasesToPath(const Node &node,
    const SCFG::SymbolBind &symbolBind,
    SCFG::InputPath &path) const
{
  const TargetPhrases *tps = node.GetTargetPhrases();
  if (tps) {
    TargetPhrases::const_iterator iter;
    for (iter = tps->begin(); iter != tps->end(); ++iter) {
      const TargetPhrase *tp = *iter;
      const SCFG::TargetPhraseImpl *tpCast = static_cast<const SCFG::TargetPhraseImpl*>(tp);
      cerr << "tpCast=" << *tpCast << endl;
      path.AddTargetPhrase(*this, symbolBind, tpCast);
    }
  }
}

}
