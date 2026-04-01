#ifndef PROJ_SOLUTION_H_
#define PROJ_SOLUTION_H_

#include <iostream>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <string>

using namespace std;

enum InstructionType {
    INT = 1,
    FP = 2,
    BRANCH = 3,
    LOAD = 4,
    STORE = 5
};

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
    
    // sequence number to track instances
    uint64_t dynamic_id; 

    // dynamic IDs instead of memory pointers
    vector<uint64_t> dependencies; 
    bool dependencies_satisfied;

    // Pipeline state
    Stage stage;
    int cycles_remaining;   // for multi-cycle stages (when D > 1)
    bool completed;
    
    // Hazard tracking
    bool is_stalled; 

    Instruction(uint64_t PC_in, InstructionType type_in, uint64_t id_in) {
        PC = PC_in;
        type = type_in;
        dynamic_id = id_in; 

        dependencies_satisfied = false;
        stage = IF;
        cycles_remaining = 1;
        completed = false;
        is_stalled = false; 
    }
};

class Pipeline {
public:
    Pipeline() {
        for (int i = 0; i < 5; i++) {
            stages[i].clear();
        }
    }

    // max 2 per stage for superscalar
    vector<Instruction*> stages[5]; 

    void Clear() {
        for (int i = 0; i < 5; i++)
            stages[i].clear();
    }
};

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

class Simulation {
public:
    Simulation(string trace_file_name, uint64_t start_inst, uint64_t inst_count, int depth_config);
    ~Simulation();

    void LoadInstructions(); 
    void RunSimulation();
    void PrintStats(); 

private:
    string trace_file_name;
    uint64_t start_inst;
    uint64_t inst_count;
    int D;

    uint64_t int_count;
    uint64_t fp_count;
    uint64_t branch_count;
    uint64_t load_count;
    uint64_t store_count;

    Pipeline pipeline;              
    Resources resources;            
    uint64_t cycle;                 
    uint64_t retired_instructions;  

    vector<Instruction*> instruction_window;                    
    
    // Hash map 
    unordered_map<uint64_t, uint64_t> latest_occurrence;    

    uint64_t fetch_index; 
    bool fetch_stalled;                             
    
    bool CheckDataHazard(Instruction* inst);        
    bool CheckStructuralHazard(Instruction* inst);  

    void Fetch();
    void Decode();      
    void Execute();
    void Memory();
    void WriteBack();

    void AdvancePipeline(); 

    int GetEXCyclesCount(Instruction* inst);     
    int GetMEMCyclesCount(Instruction* inst);    
};

#endif