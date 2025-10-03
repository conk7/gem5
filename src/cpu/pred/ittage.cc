#include "cpu/pred/ittage.hh"

#include "arch/riscv/pcstate.hh"
#include "base/logging.hh"
#include "base/random.hh"
#include "base/trace.hh"
#include "debug/Branch.hh"
#include "debug/ITTAGE_Debug.hh"

namespace gem5 {

namespace branch_prediction {

ITTAGE::ITTAGE(const Params &p)
    : IndirectPredictor(p),
      nTables(p.n_tables),
      baseTableSize(p.base_table_size),
      ghistLen(p.global_hist_len),
      longAllocThreshold(p.alloc_threshold) {
    DPRINTF(ITTAGE_Debug, "ITTAGE constructor called\n");

    tables.resize(nTables);
    tableSizes.resize(nTables);
    tagWidths.resize(nTables);
    histLengths.resize(nTables);

    tableSizes[0] = baseTableSize;
    for (unsigned i = 1; i < nTables; ++i) {
        tableSizes[i] = (p.table_sizes.empty() || i >= p.table_sizes.size())
                            ? tableSizes[i - 1] / 2
                            : p.table_sizes[i];
        if (tableSizes[i] == 0) tableSizes[i] = 1;
    }

    for (unsigned i = 0; i < nTables; ++i) {
        tagWidths[i] = (p.tag_bits.empty() || i >= p.tag_bits.size())
                           ? 8 + i * 2
                           : p.tag_bits[i];
        histLengths[i] =
            (i == 0) ? p.base_hist_len : std::min((1U << (i + 2)), ghistLen);
        tables[i].resize(tableSizes[i]);
        for (auto &entry : tables[i]) {
            entry = ITTAGEEntry();
        }
    }

    globalHist.resize(p.numThreads, std::vector<bool>(ghistLen, false));

    tableLookups.init(nTables);
    tableHits.init(nTables);

    DPRINTF(ITTAGE_Debug,
            "ITTAGE initialized with %u tables, base size %u, max history %u, "
            "alloc threshold %u\n",
            nTables, baseTableSize, ghistLen, longAllocThreshold);
}

void ITTAGE::reset() {
    DPRINTF(ITTAGE_Debug, "ITTAGE reset called\n");

    for (unsigned i = 0; i < nTables; ++i) {
        for (auto &entry : tables[i]) {
            entry = ITTAGEEntry();
        }
    }
    for (auto &hist : globalHist) {
        std::fill(hist.begin(), hist.end(), false);
    }
    DPRINTF(ITTAGE_Debug, "ITTAGE reset\n");
}

const PCStateBase *ITTAGE::lookup(ThreadID tid, InstSeqNum sn, Addr pc,
                                  void *&i_history) {
    DPRINTF(ITTAGE_Debug, "ITTAGE lookup called: tid=%d, sn=%llu, pc=%#lx\n",
            tid, sn, pc);

    lookups++;
    ITTAGEHistory *hist = new ITTAGEHistory();
    hist->table = -1;
    hist->entry = nullptr;
    hist->hit = false;
    i_history = hist;

    uint64_t histHash = 0;
    unsigned maxHistLen = 0;
    for (unsigned i = 0; i < nTables; ++i) {
        if (histLengths[i] > maxHistLen) maxHistLen = histLengths[i];
    }
    for (unsigned i = 0; i < maxHistLen && i < globalHist[tid].size(); ++i) {
        if (globalHist[tid][i]) {
            histHash ^= (1ULL << (i % 64));
        }
    }

    for (int i = nTables - 1; i >= 0; --i) {
        tableLookups[i]++;

        uint64_t idx = (pc ^ (histHash >> (i * 2))) % tableSizes[i];
        uint64_t tag = (pc ^ histHash) & ((1ULL << tagWidths[i]) - 1);

        ITTAGEEntry &entry = tables[i][idx];
        if (entry.tag == tag && entry.ctr > 0) {
            hist->table = i;
            hist->entry = &entry;
            hist->hit = true;

            hits++;
            tableHits[i]++;

            DPRINTF(ITTAGE_Debug,
                    "Lookup hit: tid=%d, sn=%llu, pc=%#lx, table=%d, "
                    "target=%#lx, ctr=%d, u=%d\n",
                    tid, sn, pc, i, entry.target, entry.ctr, entry.u);

            PCStateBase *pc_state = new RiscvISA::PCState(entry.target);
            return pc_state;
        }
    }

    misses++;
    DPRINTF(ITTAGE_Debug, "Lookup miss: tid=%d, sn=%llu, pc=%#lx\n", tid, sn,
            pc);
    return nullptr;
}

void ITTAGE::update(ThreadID tid, InstSeqNum sn, Addr pc, bool squash,
                    bool taken, const PCStateBase &target, BranchType br_type,
                    void *&i_history) {
    ITTAGEHistory *hist = static_cast<ITTAGEHistory *>(i_history);
    DPRINTF(ITTAGE_Debug,
            "ITTAGE update called: tid=%d, sn=%llu, pc=%#lx, br_type=%d\n", tid,
            sn, pc, static_cast<int>(br_type));

    if (!hist) {
        DPRINTF(ITTAGE_Debug, "Update: null history for tid=%d, sn=%llu\n", tid,
                sn);
        return;
    }

    if (br_type != BranchType::IndirectUncond &&
        br_type != BranchType::IndirectCond) {
        DPRINTF(ITTAGE_Debug, "Update: not an indirect branch, skipping\n");
        delete hist;
        i_history = nullptr;
        return;
    }

    Addr target_addr = target.instAddr();
    bool prediction_correct =
        hist->hit && hist->entry && hist->entry->target == target_addr;

    uint64_t histHash = 0;
    for (unsigned i = 0; i < ghistLen && i < globalHist[tid].size(); ++i) {
        if (globalHist[tid][i]) {
            histHash ^= (1ULL << (i % 64));
        }
    }

    if (hist->hit) {
        ITTAGEEntry *entry = const_cast<ITTAGEEntry *>(hist->entry);
        if (prediction_correct) {
            if (entry->ctr < 7) entry->ctr++;
            if (entry->u < 3) entry->u++;
        } else {
            if (entry->ctr > -8) entry->ctr--;
            if (entry->ctr <= 0 && entry->u > 0) entry->u--;
        }
        DPRINTF(ITTAGE_Debug,
                "Update: tid=%d, sn=%llu, pc=%#lx, table=%d, correct=%d, "
                "new_ctr=%d, new_u=%d\n",
                tid, sn, pc, hist->table, prediction_correct, entry->ctr,
                entry->u);
    }

    if (!prediction_correct && taken) {
        int alloc_table = -1;
        for (unsigned i = hist->hit ? hist->table + 1 : 1; i < nTables; ++i) {
            uint64_t idx = (pc ^ (histHash >> (i * 2))) % tableSizes[i];
            uint64_t tag = (pc ^ histHash) & ((1ULL << tagWidths[i]) - 1);
            ITTAGEEntry &entry = tables[i][idx];

            if (entry.ctr == 0 || (entry.u == 0 && entry.ctr <= 0)) {
                entry.target = target_addr;
                entry.tag = tag;
                entry.ctr = 1;
                entry.u = 0;
                alloc_table = i;
                allocations++;
                break;
            }
        }

        if (alloc_table != -1) {
            DPRINTF(ITTAGE_Debug,
                    "Allocated new entry: tid=%d, sn=%llu, pc=%#lx, "
                    "table=%d, target=%#lx\n",
                    tid, sn, pc, alloc_table, target_addr);
        } else if (hist->hit && hist->table < static_cast<int>(nTables) - 1) {
            for (unsigned i = hist->table + 1; i < nTables; ++i) {
                uint64_t idx = (pc ^ (histHash >> (i * 2))) % tableSizes[i];
                ITTAGEEntry &entry = tables[i][idx];
                if (entry.u == 0) {
                    entry.target = target_addr;
                    entry.tag = (pc ^ histHash) & ((1ULL << tagWidths[i]) - 1);
                    entry.ctr = 1;
                    entry.u = 0;
                    alloc_table = i;
                    allocations++;
                    break;
                }
            }
            if (alloc_table != -1) {
                DPRINTF(ITTAGE_Debug,
                        "Stole entry: tid=%d, sn=%llu, pc=%#lx, table=%d, "
                        "target=%#lx\n",
                        tid, sn, pc, alloc_table, target_addr);
            }
        }
    }

    if (taken) {
        globalHist[tid].push_back(true);
        if (globalHist[tid].size() > ghistLen) {
            globalHist[tid].erase(globalHist[tid].begin());
        }
    } else {
        globalHist[tid].push_back(false);
        if (globalHist[tid].size() > ghistLen) {
            globalHist[tid].erase(globalHist[tid].begin());
        }
    }

    delete hist;
    i_history = nullptr;
}

void ITTAGE::squash(ThreadID tid, InstSeqNum sn, void *&i_history) {
    DPRINTF(ITTAGE_Debug, "ITTAGE squash called: tid=%d, sn=%llu\n", tid, sn);

    ITTAGEHistory *hist = static_cast<ITTAGEHistory *>(i_history);
    if (hist) {
        DPRINTF(ITTAGE_Debug, "Squash: tid=%d, sn=%llu, table=%d\n", tid, sn,
                hist->table);
        delete hist;
        i_history = nullptr;
    }
}

void ITTAGE::commit(ThreadID tid, InstSeqNum sn, void *&i_history) {
    DPRINTF(ITTAGE_Debug, "ITTAGE commit called: tid=%d, sn=%llu\n", tid, sn);

    ITTAGEHistory *hist = static_cast<ITTAGEHistory *>(i_history);
    if (hist) {
        DPRINTF(ITTAGE_Debug, "Commit: tid=%d, sn=%llu, table=%d\n", tid, sn,
                hist->table);
        delete hist;
        i_history = nullptr;
    }
}

void ITTAGE::regStats() {
    IndirectPredictor::regStats();

    lookups.name(name() + ".lookups").desc("Number of lookups in ITTAGE");

    hits.name(name() + ".hits").desc("Number of hits in ITTAGE tables");

    misses.name(name() + ".misses").desc("Number of misses in ITTAGE tables");

    allocations.name(name() + ".allocations")
        .desc("Number of new entries allocated in ITTAGE tables");

    // tableLookups.name(name() + ".tableLookups")
    //     .desc("Number of lookups per table");

    // tableHits.name(name() + ".tableHits").desc("Number of hits per table");

    DPRINTF(ITTAGE_Debug, "ITTAGE statistics registered\n");
}

}  // namespace branch_prediction
}  // namespace gem5
