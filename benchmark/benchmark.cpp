#define USE_BOOST

#define JUMPING_TEST 1
//#define READ_TEST 1

#include "SeqLib/SeqLibUtils.h"

#ifdef USE_BOOST
#include <boost/timer/timer.hpp>
#endif

#include <cmath>

//#define RUN_SEQAN 1
//#define RUN_BAMTOOLS 1
#define RUN_SEQLIB 1

#ifdef RUN_SEQAN
#include <seqan/bam_io.h>
#include <seqan/sequence.h>
using namespace seqan;
#endif

#ifdef RUN_SEQLIB
#include "SeqLib/BamReader.h"
#include "SeqLib/BamWriter.h"
#endif

#define BAMTOOLS_GET_CORE 1

#ifdef RUN_BAMTOOLS
#include "api/BamReader.h"
#endif

int main()
{
  
  const size_t limit       = 5000000;
  const size_t print_limit = 1000000;
  const size_t jump_limit  = 1000;
  size_t count = 0;

  //std::string bam  = "/xchip/gistic/Jeremiah/GIT/SeqLib/seq_test/test_data/small.bam";
  std::string bam = "/broad/broadsv/NA12878/20120117_ceu_trio_b37_decoy/CEUTrio.HiSeq.WGS.b37_decoy.NA12878.clean.dedup.recal.20120117.bam";
  std::string bami = "/broad/broadsv/NA12878/20120117_ceu_trio_b37_decoy/CEUTrio.HiSeq.WGS.b37_decoy.NA12878.clean.dedup.recal.20120117.bam.bai";
  std::string obam = "/xchip/gistic/Jeremiah/GIT/SeqLib/seq_test/tmp_out.bam";

#ifdef USE_BOOST
  boost::timer::auto_cpu_timer t;
#endif

#ifdef RUN_BAMTOOLS
  std::cerr << " **** RUNNING BAMTOOLS **** " << std::endl;
  BamTools::BamReader btr;
  btr.Open(bam);
  btr.OpenIndex(bami);

  BamTools::BamAlignment ba;
  std::vector<BamTools::BamAlignment> bav;

#ifdef READ_TEST
#ifndef BAMTOOLS_GET_CORE
  std::cerr << " **** FULL REC **** " << std::endl;
  while(btr.GetNextAlignment(ba) && count++ < limit) {
#else
    std::cerr << " **** CORE REC **** " << std::endl;
  while(btr.GetNextAlignmentCore(ba) && count++ < limit) {
#endif
    if (count % print_limit == 0)
      std::cerr << "...at read " << SeqLib::AddCommas(count) << std::endl;
    bav.push_back(ba);
  }
#endif

#ifdef JUMPING_TEST
  // perform jumping
  for (int i = 0; i < jump_limit; ++i) {
    int chr = rand() % 22;
    int pos = rand() % 1000000 + 1000000;
    if (btr.SetRegion(BamTools::BamRegion(chr,chr, pos, pos + 10000))) {
      btr.GetNextAlignment(ba);
      bav.push_back(ba);
    } else {
      std::cerr << " jump to " << chr << "-" << pos << " not successful " << std::endl;
    }
  }
#endif

#endif    

#ifdef RUN_SEQLIB
  std::cerr << " **** RUNNING SEQLIB **** " << std::endl;
  SeqLib::BamReader r;
  r.Open(bam);
  //SeqLib::BamWriter w(SeqLib::BAM);
  //w.SetHeader(r.Header());
  //w.Open(obam);


  SeqLib::BamRecord rec;
  SeqLib::BamRecordVector bav;
#ifdef READ_TEST
  std::vector<std::string> sq;
  while(r.GetNextRecord(rec) && count++ < limit) {
    if (count % print_limit == 0)
      std::cerr << "...at read " << SeqLib::AddCommas(count) << std::endl;
    bav.push_back(rec);
    //sq.push_back(rec.Sequence());
  }
#endif

#ifdef JUMPING_TEST
  // perform jumping test
  for (int i = 0; i < jump_limit; ++i) {
    int chr = rand() % 22;
    int pos = rand() % 1000000 + 1000000;
    r.SetRegion(SeqLib::GenomicRegion(chr,pos, pos + 10000));
    r.GetNextRecord(rec);
    bav.push_back(rec);
  }
#endif

#endif

#ifdef RUN_SEQAN

  std::cerr << " **** RUNNING SEQAN **** " << std::endl;

  seqan::BamFileIn bamFileIn;
  seqan::BamHeader header;

  std::vector<seqan::BamAlignmentRecord> bav;
  seqan::BamAlignmentRecord record;

  if (!open(bamFileIn, bam.c_str(), seqan::OPEN_RDONLY))
    {
      std::cerr << "ERROR: could not open input file " << bam << ".\n";
      return 1;
    }

  // Open output SAM file.
  //seqan::BamFileOut samFileOut(context(bamFileIn), obam.c_str());
  
  // Copy header.
  try
    {
      readHeader(header, bamFileIn);
      //writeHeader(samFileOut, header);
    }
  catch (seqan::IOError const & e)
    {
      std::cerr << "ERROR: could not copy header. " << e.what() << "\n";
    }

#ifdef JUMPING_TEST
  // read the index
  BamIndex<Bai> baiIndex;
  if (!open(baiIndex, bami.c_str()))
    {
      std::cerr << "ERROR: Could not read BAI index file " << bami << "\n";
      return 1;
    }

  bool hasAlignments = false;
  for (int i = 0; i < jump_limit; ++i) {
    int chr = rand() % 22;
    int pos = rand() % 1000000 + 1000000;
    if (!jumpToRegion(bamFileIn, hasAlignments, chr, pos, pos+10000, baiIndex)) {
      std::cerr << "ERROR: Could not jump to " << pos << ":" << (pos+10000) << "\n";
        return 1;
      }
    if (hasAlignments) {
	  readRecord(record, bamFileIn);
	  bav.push_back(record);
    } else {
      std::cerr << "no alignments here " << std::endl;
    }

      
  }

#endif

#ifdef READ_TEST
  // Copy all records.
  while (!atEnd(bamFileIn) && count++ < limit)
    {
      try
	{
	  if (count % print_limit == 0)
	    std::cerr << "...at read " << SeqLib::AddCommas(count) << std::endl;
	  readRecord(record, bamFileIn);
	  bav.push_back(record);
	  //writeRecord(samFileOut, record);
	}
      catch (seqan::IOError const & e)
	{
	  std::cerr << "ERROR: could not copy record. " << e.what() << "\n";
	}
    }
#endif

#endif

  std::cerr << " Copied " << bav.size() << " records " << std::endl;

  return 0;
}
