#ifndef PROJ_SOLUTION_H_
#define PROJ_SOLUTION_H_

#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <cstdint>
#include <string>

using namespace std;

// Instruction Types
enum InstructionType {
    INT = 1,
    FP = 2,
    BRANCH = 3,
    LOAD = 4,
    STORE = 5
};


// Pipeline Stages
enum Stage {
    IF = 0,
    ID = 1,
    EX = 2,
    MEM = 3,
    WB = 4
};


// Instruction Structure
struct Instruction {
    uint64_t PC;
    InstructionType type;

    // Dependency tracking
    vector<Instruction*> dependencies;
    bool dependencies_satisfied;

    // Pipeline state
    Stage stage;
    int cycles_remaining;   // for multi-cycle stages (when D > 1)
    bool completed;

    Instruction(uint64_t PC_in, InstructionType type_in) {
        PC = PC_in;
        type = type_in;

        dependencies_satisfied = false;

        stage = IF;
        cycles_remaining = 1;
        completed = false;
    }
};

// Pipeline (2-wide superscalar)
class Pipeline {
public:
    Pipeline() {
        for (int i = 0; i < 5; i++) {
            stages[i].clear();
        }
    }

    // stages[0] = IF, stages[1] = ID, stages[2] = EX, stages[3] = MEM, stages[4] = WB
    // Each stage holds up to 2 instructions
    vector<Instruction*> stages[5]; 

    void Clear() {
        for (int i = 0; i < 5; i++)
            stages[i].clear();
    }
};


// Resource Tracking (per cycle)
struct Resources {
    bool int_inUse;
    bool fp_inUse;
    bool branch_inUse;
    bool mem_read_inUse;
    bool mem_write_inUse;

    void Reset() {
        int_inUse = false;
        fp_inUse = false;
        branch_inUse = false;
        mem_read_inUse = false;
        mem_write_inUse = false;
    }
};

// Simulation Class
class Simulation {
public:
    Simulation(string trace_file_name, uint64_t start_inst,
               uint64_t inst_count, int depth_config);

    ~Simulation();

    void RunSimulation();

private:
    // Input Parameters
    string trace_file_name;
    uint64_t start_inst;
    uint64_t inst_count;
    int D;

    // Statistics
    uint64_t int_count;
    uint64_t fp_count;
    uint64_t branch_count;
    uint64_t load_count;
    uint64_t store_count;

    // State
    Pipeline pipeline;              // 5 stages, 2 instructions per stage
    Resources resources;            // Hardware resource usage tracking for current cycle
    uint64_t cycle;                 // Current cycle number
    uint64_t retired_instructions;  // Total instructions finished so far

    // Instruction Storage
    vector<Instruction*> instruction_window;                    // Stores all (inst_count) instructions for current simulation run
    unordered_map<uint64_t, Instruction*> latest_occurrence;    // Maps each PC to last dynamic instance of that instruction for dependency tracking

    bool fetch_stalled;                             // When IF stage is blocked due to a branch (control hazard)
    bool CheckDataHazard(Instruction* inst);        // Checks if data dependencies are satisfied
    bool CheckStructuralHazard(Instruction* inst);  // Checks if required hardware resource is available

    void Fetch();
    void Decode();      
    void Execute();
    void Memory();
    void WriteBack();

    void AdvancePipeline(); // Move instructions to next stage if possible

    void LoadInstructions(); // Load instructions from trace file into instruction_window

    int GetEXCyclesCount(Instruction* inst);     // Get number of cycles needed in EX stage (depends on instruction type and D)
    int GetMEMCyclesCount(Instruction* inst);    // Get number of cycles needed in MEM stage (depends on instruction type and D)

    void UpdateStats(Instruction* inst); // NOT CERTAIN ABOUT THIS ONE, MAYBE UPDATE IN OTHER PLACES
    // WE NEED A FUNCTION TO PRINT STATS
};

#endif
