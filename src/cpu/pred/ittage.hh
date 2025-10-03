#ifndef __CPU_PRED_ITTAGE_HH__
#define __CPU_PRED_ITTAGE_HH__

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/pred/indirect.hh"
#include "params/ITTAGE.hh"

namespace gem5 {

namespace branch_prediction {

struct ITTAGEEntry {
    Addr target;
    unsigned tag;
    int8_t ctr;
    uint8_t u;
    ITTAGEEntry() : target(0), tag(0), ctr(0), u(0) {}
};

struct ITTAGEHistory {
    int table;
    const ITTAGEEntry *entry;
    bool hit;
};

class ITTAGE : public IndirectPredictor {
   public:
    typedef ITTAGEParams Params;

    ITTAGE(const Params &p);

    void reset() override;

    const PCStateBase *lookup(ThreadID tid, InstSeqNum sn, Addr pc,
                              void *&i_history) override;

    void update(ThreadID tid, InstSeqNum sn, Addr pc, bool squash, bool taken,
                const PCStateBase &target, BranchType br_type,
                void *&i_history) override;

    void squash(ThreadID tid, InstSeqNum sn, void *&i_history) override;

    void commit(ThreadID tid, InstSeqNum sn, void *&i_history) override;

    void regStats() override;

   private:
    unsigned nTables;
    unsigned baseTableSize;
    unsigned ghistLen;
    unsigned longAllocThreshold;

    std::vector<std::vector<ITTAGEEntry>> tables;
    std::vector<unsigned> tableSizes;
    std::vector<unsigned> tagWidths;
    std::vector<unsigned> histLengths;
    std::vector<std::vector<bool>> globalHist;

    statistics::Scalar lookups;
    statistics::Scalar hits;
    statistics::Scalar misses;
    statistics::Scalar allocations;
    statistics::Vector tableLookups;
    statistics::Vector tableHits;
};

}  // namespace branch_prediction
}  // namespace gem5

#endif  // __CPU_PRED_ITTAGE_HH__
