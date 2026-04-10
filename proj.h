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
    bool dependencies_translated;

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

        dependencies_translated = false;
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
    Simulation(string trace_file_name, uint64_t start_inst, uint64_t inst_count, int depth_config){
        this->trace_file_name = trace_file_name;
        this->start_inst = start_inst;
        this->inst_count = inst_count;
        this->D = depth_config;
        
        int_count = 0; fp_count = 0; branch_count = 0; load_count = 0; store_count = 0;
        
        cycle = 0;
        fetch_index = 0; 
        retired_instructions = 0;
        fetch_stalled = false;
        
        resources.Reset();
    };

    ~Simulation(){
        for (Instruction* inst : instruction_window) {
            delete inst;
        }
    };

    void LoadInstructions(); 
    void RunSimulation();

    void PrintStats(){
        // Total completed instructions
        uint64_t total = retired_instructions;

        // Division by zero check
        if (total == 0) {
            printf("\nSimulation Results:\n");
            printf("No instructions retired.\n");
            printf("Total cycles = %llu\n", cycle);
            return;
        }

        // Instruction %
        double int_percent    = (double)int_count    * 100.0 / total;
        double fp_percent    = (double)fp_count     * 100.0 / total;
        double branch_percent = (double)branch_count * 100.0 / total;
        double load_percent   = (double)load_count   * 100.0 / total;
        double store_percent  = (double)store_count  * 100.0 / total;


        // Frequency based on D
        double freq;
        if (D == 1) freq = 1.0;
        else if (D == 2) freq = 1.2;
        else if (D == 3) freq = 1.7;
        else if (D == 4) freq = 1.8;
        else freq = 1.0; 

        // Execution time
        double exec_time = (double)cycle / (freq * 1e6);

        printf("\nSimulation Results:\n");

        printf("Total cycles = %llu\n", cycle);
        printf("Total instructions retired = %llu\n", retired_instructions);

        printf("Execution time = %.6f ms\n", exec_time);

        printf("\nInstruction mix:\n");
        printf("INT    = %.2f%%\n", int_percent);
        printf("FP     = %.2f%%\n", fp_percent);
        printf("BRANCH = %.2f%%\n", branch_percent);
        printf("LOAD   = %.2f%%\n", load_percent);
        printf("STORE  = %.2f%%\n", store_percent);
    }; 

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
