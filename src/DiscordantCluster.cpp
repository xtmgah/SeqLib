#include "SnowTools/DiscordantCluster.h"

#include <cassert>
#include <set>
#include <numeric>

#define DISC_PAD 400
#define MIN_PER_CLUST 2

namespace SnowTools {
  
  void DiscordantCluster::addRead(std::string name) {
    std::unordered_map<std::string, bool>::iterator ff = qnames.find(name);
    if (ff == qnames.end())
      qnames.insert(std::pair<std::string, bool>(name, true));
    return;
  }

  DiscordantClusterMap DiscordantCluster::clusterReads(const BamReadVector& bav, const GenomicRegion& interval) {

    // remove any reads that are not present twice or have sufficient isize
    std::unordered_map<std::string, int> tmp_map;
    for (auto& i : bav) {
      std::string tt = i.Qname();
      if (tmp_map.count(tt) ==0)
	tmp_map[tt] = 1;
      else
	tmp_map[tt]++;
    }

    BamReadVector bav_dd;
    for (auto& r : bav) {

      // is the read even discordant?
      bool disc_r = (abs(r.InsertSize()) >= 600) || (r.MateChrID() != r.ChrID());
      if (tmp_map[r.Qname()] == 2 && disc_r)
	bav_dd.push_back(r);

    }

    // sort by position
    std::sort(bav_dd.begin(), bav_dd.end(), BamReadSort::ByReadPosition());
    
    // clear the tmp map. Now we want to use it to store if we already clustered read
    tmp_map.clear();

    BamReadClusterVector fwd, rev, fwdfwd, revrev, fwdrev, revfwd;
    std::pair<int, int> fwd_info, rev_info; // refid, pos
    fwd_info = {-1,-1};
    rev_info = {-1,-1};

    // make the fwd and reverse READ clusters. dont consider mate yet
    __cluster_reads(bav_dd, fwd, rev);
    
    // within the forward read clusters, cluster mates on fwd and rev
    __cluster_mate_reads(fwd, fwdfwd, fwdrev);

    // within the reverse read clusters, cluster mates on fwd and rev
    __cluster_mate_reads(rev, revfwd, revrev);

    // we have the reads in their clusters. Just convert to discordant reads clusters
    DiscordantClusterMap dd;
    __convertToDiscordantCluster(dd, fwdfwd, bav_dd);
    __convertToDiscordantCluster(dd, fwdrev, bav_dd);
    __convertToDiscordantCluster(dd, revfwd, bav_dd);
    __convertToDiscordantCluster(dd, revrev, bav_dd);
    
    // remove clusters that dont overlap with the window
    DiscordantClusterMap dd_clean;
    for (auto& i : dd) {
      if (interval.isEmpty() /* whole genome */ || i.second.m_reg1.getOverlap(interval) > 0 || i.second.m_reg2.getOverlap(interval))
	dd_clean[i.first] = i.second;
    }
    
    return dd_clean;

  }
  
  // this reads is reads in the cluster. all_reads is big pile where all the clusters came from
  DiscordantCluster::DiscordantCluster(const BamReadVector& this_reads, const BamReadVector& all_reads) {
    
    if (this_reads.size() == 0)
      return;
    if (all_reads.size() == 0)
      return;
    
    // check the orientations, fill the reads
    bool rev = this_reads[0].ReverseFlag();
    bool mrev = this_reads[0].MateReverseFlag();
    
    // the ID is just the first reads Qname
    m_id = this_reads[0].Qname();
    assert(m_id.length());
    
    for (auto& i : this_reads) 
      {
	
	// double check that we did the clustering correctly. All read orientations should be same
	assert(rev == i.ReverseFlag() && mrev == i.MateReverseFlag()); 

	// add the read to the read map
	std::string tmp = i.GetZTag("SR");
	assert(tmp.length());
	reads[tmp] = i;

	// set the qname map
	std::string qn = i.Qname();
	qnames[qn] = true;

	// the ID is the lexographically lowest qname
	if (qn < m_id)
	  m_id = qn;
	if (tmp.at(0) == 't')
	  ++tcount;
	else
	  ++ncount;
      }

    // loop through the big stack of reads and find the mates
    addMateReads(all_reads);
    //std::cerr << reads.size() << " mates " << mates.size() << " this " << this_reads.size() << " all " << all_reads.size() << std::endl;

    //assert(reads.size() == mates.size());
    assert(reads.size() > 0);
    
    // set the regions
    m_reg1 = SnowTools::GenomicRegion(-1,500000000,-1); // read region
    for (auto& i : reads) 
      {
	m_reg1.strand = i.second.ReverseFlag() ? '-' : '+'; //r_strand(i.second) == '+'; //(!i.second->IsReverseStrand()) ? '+' : '-';
	m_reg1.chr = i.second.ChrID(); //r_id(i.second); //i.second->RefID;
	if (i.second.Position() < m_reg1.pos1)
	  m_reg1.pos1 = i.second.Position(); //r_pos(i.second); //i.second->Position;
	int endpos = i.second.PositionEnd(); //r_endpos(i.second);
	if (endpos > m_reg1.pos2)
	  m_reg1.pos2 = endpos;
      }
    
    m_reg2 = SnowTools::GenomicRegion(-1,500000000,-1); // mate region
    for (auto& i : mates) 
      {
	m_reg2.strand = i.second.ReverseFlag() ? '-' : '+'; //r_strand(i.second) == '+'; //(!i.second->IsReverseStrand()) ? '+' : '-';
	m_reg2.chr = i.second.ChrID(); //i.second->RefID;
	if (i.second.Position() < m_reg2.pos1)
	  m_reg2.pos1 = i.second.Position();
	int endpos = i.second.PositionEnd();
	if (endpos > m_reg2.pos2)
	  m_reg2.pos2 = endpos;
      }


    // orient them correctly so that left end is first
    if (m_reg2 < m_reg1) {
      GenomicRegion tmp_reg = m_reg1;
      m_reg1 = m_reg2;
      m_reg2 = tmp_reg;

      std::unordered_map<std::string, BamRead> tmp_reads = reads;
      reads = mates;
      mates = tmp_reads;
    }

  }
  
  void DiscordantCluster::addMateReads(const BamReadVector& bav) 
  { 
    
    for (auto& i : bav) {
      std::string sr;
      if (qnames.count(i.Qname())) 
	{
	  std::string tmp = i.GetZTag("SR");
	  if (reads.count(tmp) == 0) // only add if this is a mate read
	    mates[tmp] = i;
	}
    }
    
  }
  
  double DiscordantCluster::getMeanMapq(bool mate) const 
  {
    double mean = 0;
    std::vector<int> tmapq;
    if (mate) {
      for (auto& i : mates)
	tmapq.push_back(i.second.MapQuality());
    } else {
      for (auto& i : reads)
	tmapq.push_back(i.second.MapQuality());
    }
    
    if (tmapq.size() > 0)
      mean = std::accumulate(tmapq.begin(), tmapq.end(), 0.0) / tmapq.size();
    return mean;
  }
  
  std::string DiscordantCluster::toRegionString() const 
  {
    int pos1 = (m_reg1.strand == '+') ? m_reg1.pos2 : m_reg1.pos1;
    int pos2 = (m_reg2.strand == '+') ? m_reg2.pos2 : m_reg2.pos1;
    
    std::stringstream ss;
    ss << m_reg1.chr+1 << ":" << pos1 << "(" << m_reg1.strand << ")" << "-" << 
      m_reg2.chr+1 << ":" << pos2 << "(" << m_reg2.strand << ")";
    return ss.str();
    
  }
  
  // define how to print this to stdout
  std::ostream& operator<<(std::ostream& out, const DiscordantCluster& dc) 
  {
    
    out << dc.toRegionString() << " Tcount: " << dc.tcount << 
      " Ncount: "  << dc.ncount << " Mean MAPQ: " 
	<< dc.getMeanMapq(false) << " Mean Mate MAPQ: " << dc.getMeanMapq(true);
    /*for (auto& i : dc.reads) {
      std::string tmp;
      i.second->GetTag("SR",tmp);
      out << "   " << i.second->RefID << ":" << i.second->Position << (i.second->IsReverseStrand() ? "(-)" : "(+)") << " - " << i.second->MateRefID << ":" << i.second->MatePosition <<  (i.second->IsMateReverseStrand() ? "(-)" : "(+)") << " " << tmp << endl;
      }*/
    return out;
  }
  
  
  // define how to print to file
  std::string DiscordantCluster::toFileString(bool with_read_names /* false */) const 
  { 
    
    std::string sep = "\t";
    
    // add the reads names (currently off)
    std::string reads_string = "x";
    if (with_read_names) 
      {
      for (auto& i : reads) 
	{
	  std::string tmp = i.second.GetZTag("SR");
	  reads_string += tmp + ",";
	}
      
      //debug
      if (reads_string.length() == 0)
	reads_string = "x";
      else
	reads_string = reads_string.substr(0,reads_string.length() - 1);
      }
    
    std::stringstream out;
    out << m_reg1.chr+1 << sep << m_reg1.pos1 << sep << m_reg1.strand << sep 
	<< m_reg2.chr+1 << sep << m_reg2.pos1 << sep << m_reg2.strand << sep 
	<< tcount << sep << ncount << sep << getMeanMapq(false) << sep 
	<< getMeanMapq(true) << sep << reads_string;

    return (out.str());
    
  }
  
  // define how to sort theses
  bool DiscordantCluster::operator<(const DiscordantCluster &b) const 
  {
    if (m_reg1.chr < b.m_reg1.chr)
      return true;
    if (m_reg1.pos1 < b.m_reg1.pos1)
      return true;
    return false;
    
  }

  /**
   * Cluster reads by alignment position 
   * 
   * Checks whether a read belongs to a cluster. If so, adds it. If not, ends
   * and stores cluster, adds a new one.
   *
   * @param cvec Stores the vector of clusters, which themselves are vectors of read pointers
   * @param clust The current cluster that is being added to
   * @param a Read to add to cluster
   * @param mate Flag to specify if we should cluster on mate position instead of read position
   * @return Description of the return value
   */
  bool DiscordantCluster::__add_read_to_cluster(BamReadClusterVector &cvec, BamReadVector &clust, const BamRead &a, bool mate) {

    // get the position of the previous read. If none, we're starting a new one so make a dummy
    std::pair<int,int> last_info;
    if (clust.size() == 0)
      last_info = {-1, -1};
    else if (mate)
      last_info = {clust.back().MateChrID(), clust.back().MatePosition()};
    else
      last_info = {clust.back().ChrID(), clust.back().Position()};
    
    // get the position of the current read
    std::pair<int,int> this_info;
    if (mate)
      this_info = {a.MateChrID(), a.MatePosition()};
    else 
      this_info = {a.ChrID(), a.Position()};
    
    // check if this read is close enough to the last
    if ( (this_info.first == last_info.first) && (this_info.second - last_info.second) <= DISC_PAD ) {
      // read belongs in current cluster, so add
      clust.push_back(a);
      last_info = this_info;
      return true;
      
    // read does not belong to cluster. close this cluster and add to cvec
    } else {
      
      // if enough supporting reads, add as a cluster
      if (clust.size() >= MIN_PER_CLUST) 
	cvec.push_back(clust);
      
      // clear this cluster and start a new one
      clust.clear();
      clust.push_back(a);
      
      return false;
    }
  }

  void DiscordantCluster::__cluster_mate_reads(BamReadClusterVector& brcv, BamReadClusterVector& fwd, BamReadClusterVector& rev)
  {
    // loop through the clusters, and cluster within clusters based on mate read
    for (auto& v : brcv) 
      {
	BamReadVector this_fwd, this_rev;
	std::sort(v.begin(), v.end(), BamReadSort::ByMatePosition());
	for (auto& r : v) 
	  {
	    // forward clustering
	    if (!r.MateReverseFlag()) 
	      __add_read_to_cluster(fwd, this_fwd, r, true);
	    // reverse clustering 
	    else 
	      __add_read_to_cluster(rev, this_rev, r, true);
	    
	  }
	// finish the last clusters
	if (this_fwd.size() > 0)
	  fwd.push_back(this_fwd);
	if (this_rev.size() > 0)
	  rev.push_back(this_rev);
      } // finish main cluster loop
  }
  
  void DiscordantCluster::__cluster_reads(const BamReadVector& brv, BamReadClusterVector& fwd, BamReadClusterVector& rev) 
  {

    // hold the current cluster
    BamReadVector this_fwd, this_rev;

    std::set<std::string> tmp_set;
    
    // cluster in the READ direction, separately for fwd and rev
    for (auto& i : brv) {
      std::string qq = i.Qname();

      // only cluster if not seen before (e.g. left-most is READ, right most is MATE)
      if (i.PairMappedFlag() && tmp_set.count(qq) == 0) {

	tmp_set.insert(qq);

	// forward clustering
	if (!i.ReverseFlag()) 
	  __add_read_to_cluster(fwd, this_fwd, i, false);
	// reverse clustering 
	else 
	  __add_read_to_cluster(rev, this_rev, i, false);
      }
    }
    // finish the last clusters
    if (this_fwd.size() > 0)
      fwd.push_back(this_fwd);
    if (this_rev.size() > 0)
      rev.push_back(this_rev);

  }

  void DiscordantCluster::__convertToDiscordantCluster(DiscordantClusterMap &dd, const BamReadClusterVector& cvec, const BamReadVector& bav) {
    
    for (auto& v : cvec) {
      if (v.size() > 1) {
	DiscordantCluster d(v, bav); /// slow but works
	dd[d.m_id] = d;
      }
    }
  }
  
  GenomicRegion DiscordantCluster::GetMateRegionOfOverlap(const GenomicRegion& gr) const {
    
    if (gr.getOverlap(m_reg1))
      return m_reg2;
    if (gr.getOverlap(m_reg2))
      return m_reg1;
    return GenomicRegion();

  }

  
}
