from gem5.resources.resource import BinaryResource
from gem5.simulate.simulator import Simulator
from m5.objects import ITTAGE, TournamentBP, SimpleBTB, ReturnAddrStack

from gem5.prebuilt.riscvmatched.riscvmatched_board import RISCVMatchedBoard


class U74BP(TournamentBP):
    btb = SimpleBTB(numEntries=32)
    ras = ReturnAddrStack(numEntries=12)  #
    localHistoryTableSize = 4096
    localPredictorSize = 16384
    globalPredictorSize = 16384
    choicePredictorSize = 16384
    localCtrBits = 4
    globalCtrBits = 4
    choiceCtrBits = 4
    indirectBranchPred = ITTAGE(
        n_tables=2,
        base_table_size=256,
        global_hist_len=64,
        base_hist_len=4,
        alloc_threshold=2,
        table_sizes=[32, 64],
        tag_bits=[10, 10],
        numThreads=1,
    )


branch_predictor = U74BP()

board = RISCVMatchedBoard()
board.processor.cores[0].core.branchPred = branch_predictor

workload_id = "bench"
binary = BinaryResource(
    # local_path="/home/conk/Files/Programming/gem5_microbench/microbench/CS1/bench.RISCV"
    local_path="/home/conk/Files/Programming/gem5/experiments/bench"
)
board.set_se_binary_workload(binary=binary, arguments=[1])

print(f"Simulation starting with workload: {workload_id}")

simulator = Simulator(board=board)
simulator.run()

print("Simulation finished")
