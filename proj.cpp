#include "proj.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>

Simulation::Simulation(string trace_file_name, uint64_t start_inst, uint64_t inst_count, int depth_config) {
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
}

Simulation::~Simulation() {
    for (Instruction* inst : instruction_window) {
        delete inst;
    }
}

void Simulation::LoadInstructions() {
    //read the file 
    std::ifstream file(trace_file_name);
    //verify and check if loading failed
    if (!file.is_open()) {
        std::cerr << "Error: Could not open trace file\n";
        return;
    }
    //holds each line in string 
    std::string line;
    //current line
    uint64_t current_line_num = 0;
    //number of instructions loaded so far
    uint64_t loaded_count = 0;
    //every instruction gets a unique dynamic ID as it is loaded, starting from 1
    uint64_t dynamic_id_counter = 1;

    std::cout << "Loading instructions from " << trace_file_name << "...\n";
    //Enters an I/O loop, reading the file until the EOF is reached.
    while (std::getline(file, line)) {
        current_line_num++;
        //move to start point by user
        if (current_line_num < start_inst) continue; 
        //terminate if limit reached
        if (loaded_count >= inst_count) break;

        //comma separated values: PC, type, dependencies
        std::stringstream ss(line);
        std::string token;
        std::getline(ss, token, ',');
        uint64_t pc = std::stoull(token, nullptr, 16);
        std::getline(ss, token, ',');

        // Convert the instruction type from string to enum
        InstructionType type = static_cast<InstructionType>(std::stoi(token));

        // Create a new instruction object with parsed PC, type, and a unique dynamic ID
        Instruction* inst = new Instruction(pc, type, dynamic_id_counter++);

        //extract the remaining tokens (the static PC dependencies).
        while (std::getline(ss, token, ',')) {
            if (!token.empty() && token.back() == '\r') token.pop_back(); 
            if (!token.empty()) {
                inst->dependencies.push_back(std::stoull(token, nullptr, 16));
            }
        }

        // Add the instruction to the window and update the count
        instruction_window.push_back(inst);
        loaded_count++;
    }

    //close the file
    file.close();
    std::cout << "Successfully loaded " << instruction_window.size() << " instructions.\n\n";
}

int Simulation::GetEXCyclesCount(Instruction* inst) {
    // If the instruction is a Floating Point operation AND the pipeline (D) is set to 2 or 4, it requires 2 clock cycles.
    if (inst->type == FP && (D == 2 || D == 4)) return 2;
    return 1;
}

int Simulation::GetMEMCyclesCount(Instruction* inst) {
    // If the instruction is a LOAD AND the pipeline (D)is set to 3 or 4, retrieving from memory takes 3 clock cycles.
    if (inst->type == LOAD && (D == 3 || D == 4)) return 3;
    return 1;
}

bool Simulation::CheckDataHazard(Instruction* inst) { 
    // Loop through every dynamic ID this instruction depends on
    for (uint64_t dep_id : inst->dependencies) {
        // Scan the downstream pipeline stages (Execute, Memory, WriteBack)
        for (int stage = EX; stage <= WB; stage++) {
            //active there?
            for (Instruction* active_inst : pipeline.stages[stage]) {
                // if the active instruction's ID matches the dependency ID, the data is not ready yet. Return true(HAZARD))
                if (active_inst->dynamic_id == dep_id) return true; 
            }
        }
    }

    // If we scanned the active pipeline and didn't find the dependencies, instructions already finished. Return false (safe!).
    return false; 
}

bool Simulation::CheckStructuralHazard(Instruction* inst) { 
    // Checks if the specific unit required by this instruction is already occupied by another instruction in the current clock cycle.
    switch (inst->type) {
        //INT ALU busy?
        case INT:    return resources.int_inUse;
        //fpu
        case FP:     return resources.fp_inUse;
        //branch
        case BRANCH: return resources.branch_inUse;
        //memory read
        case LOAD:   return resources.mem_read_inUse;
        //memory write
        case STORE:  return resources.mem_write_inUse;
        default:     return false;
    }
}

void Simulation::Decode() {
    // For each instruction in D check for hazards and determine if it can proceed to Execute.
    for (size_t i = 0; i < pipeline.stages[ID].size(); i++) {
        // pointer to the instruction being evaluated
        Instruction* inst = pipeline.stages[ID][i];
        // Start by assuming the instruction can move forward this cycle
        inst->is_stalled = false; 

        if (!inst->dependencies_satisfied) {
            //Update dependencies from static PCs to exact dynamic IDs using the hash map 
            vector<uint64_t> translated_dynamic_ids;
            for (uint64_t static_pc : inst->dependencies) {
                if (latest_occurrence.count(static_pc) > 0) {
                    // Find the newest dynamic ID assigned to that PC
                    translated_dynamic_ids.push_back(latest_occurrence[static_pc]);
                }
            }
            // Overwrite the old PCs with the exact IDs we need to wait for
            inst->dependencies = translated_dynamic_ids;
            inst->dependencies_satisfied = true; 
        }
        //Check if we need to wait for data or if the required execution unit is busy. If either is true, we stall this instruction 
        bool data_hazard = CheckDataHazard(inst);
        bool structural_hazard = CheckStructuralHazard(inst);
        //if any of above true, mark stalled 
        if (data_hazard || structural_hazard) {
            inst->is_stalled = true; 
            
            //if the first instruction stalls, the second one is stall too.
            if (i + 1 < pipeline.stages[ID].size()) {
                pipeline.stages[ID][i+1]->is_stalled = true;
            }
            break; 
        } else {
            // If no hazards, mark the required resource as in use for this cycle
            switch (inst->type) {
                case INT:    resources.int_inUse = true; break;
                case FP:     resources.fp_inUse = true; break;
                case BRANCH: resources.branch_inUse = true; break;
                case LOAD:   resources.mem_read_inUse = true; break;
                case STORE:  resources.mem_write_inUse = true; break;
            }
        }
    }
}

void Simulation::Fetch() {
    int fetched_this_cycle = 0;

    while (fetched_this_cycle < 2 && fetch_index < instruction_window.size() && pipeline.stages[IF].size() < 2) {
        // Get the instruction to fetch
        Instruction* inst = instruction_window[fetch_index];
        // Place it into the IF stage of the pipeline and update the hash map with its dynamic ID 
        pipeline.stages[IF].push_back(inst);
        latest_occurrence[inst->PC] = inst->dynamic_id; // map update
        fetch_index++;
        fetched_this_cycle++;
    }
}

void Simulation::RunSimulation() {
    while (retired_instructions < inst_count) {
        cycle++;
        resources.Reset(); 

        WriteBack();
        Memory();
        Execute();
        Decode();
        
        if (!fetch_stalled) {
            Fetch(); 
        }

        AdvancePipeline();
    }
}

int main(int argc, char* argv[]) {

    // Check number of arguments
    if (argc != 5) {
        printf("Usage: ./sim <trace_file> <start_inst> <inst_count> <depth_config>\n");
        return 1;
    }

    // Input arguments
    string trace_file_name = argv[1];
    uint64_t start_inst = std::stoull(argv[2]);
    uint64_t inst_count = std::stoull(argv[3]);
    int D = std::stoi(argv[4]);

    // Error checking
    if (start_inst < 0 || inst_count <= 0) {
        printf("Input Error: start_inst and inst_count must be positive.\n");
        printf("Terminating Simulation...\n");
        return 1;
    }

    if (D < 1 || D > 4) {
        printf("Input Error: depth_config (D) must be between 1 and 4.\n");
        printf("Terminating Simulation...\n");
        return 1;
    }

    std::ifstream file(trace_file_name);
    if (!file.is_open()) {
        printf("Input Error: Trace file could not be opened.\n");
        printf("Terminating Simulation...\n");
        return 1;
    }
    file.close();

    // Run simulation
    printf("Running simulation with:\n");
    printf("Trace file = %s, start_inst = %llu, inst_count = %llu, D = %d\n",
           trace_file_name.c_str(), start_inst, inst_count, D);

    Simulation sim(trace_file_name, start_inst, inst_count, D);

    sim.LoadInstructions();
    sim.RunSimulation();
    sim.PrintStats();

    return 0;
}

void Simulation::WriteBack() {
    for (size_t i = 0; i < pipeline.stages[WB].size(); i++) {
    // pointer to the instruction being evaluated
    Instruction* inst = pipeline.stages[WB][i];

    // update statistics
    switch (inst->type) {
        case INT:    int_count++;       break;
        case FP:     fp_count++;        break;
        case BRANCH: branch_count++;    break;
        case LOAD:   load_count++;      break;
        case STORE:  store_count++;     break;
    }
    retired_instructions++;

    // not sure if completed should be set true now or during Memory/Execute 
    inst->completed = true;         
    }
}

void Simulation::Memory() {
    // empty memory queue
    if (pipeline.stages[MEM].size() == 0)
        return;

    // assume instruction is not stalled initially
    Instruction* first = pipeline.stages[MEM][0];
    first->is_stalled = false;
    first->cycles_remaining--;

    // First instruction not finished all substages 
    if (first->cycles_remaining > 0)
        first->is_stalled = true;

    // 1 instruction in memory queue
    if (pipeline.stages[MEM].size() == 1){
        return;
    } // 2 instructions in memory queue
    else{           
        Instruction* second = pipeline.stages[MEM][1];
        second->is_stalled = false;

        // 2 conditions to stop second instruction:
        // 1. First instruciton is stalled
        // 2. Both instructions are Load/Store functions
        if (first->is_stalled || (first->type == second->type && (first->type == LOAD || first->type == STORE))){
            second->is_stalled = true;
        } else{
            second->cycles_remaining--;
        }
    }
    return;
}

void Simulation::Execute() {
    // empty EX queue
    if (pipeline.stages[EX].size() == 0)
        return;

    // assume instruction is not stalled initially
    Instruction* first = pipeline.stages[EX][0];
    first->is_stalled = false;
    first->cycles_remaining--;

    // First instruction not finished all substages 
    if (first->cycles_remaining > 0)
        first->is_stalled = true;

    // 1 instruction in memory queue
    if (pipeline.stages[EX].size() == 1){
        return;
    } // 2 instructions in EX queue
    else{   
        switch (first->type) {
            case INT:    resources.int_inUse = true;        break;
            case FP:     resources.fp_inUse = true;         break;
            case BRANCH: resources.branch_inUse = true;     break;
            case LOAD:   resources.mem_read_inUse = true;   break;
            case STORE:  resources.mem_write_inUse = true;  break;
        }
        Instruction* second = pipeline.stages[MEM][1];
        second->is_stalled = false;
        // 2 conditions to stop second instruction:
        // 1. both instructions are the same instruction type 
        // (Load and store can't go into MEM effectively means they cannot execute in the same cycle)
        // 2. first instruction is stalled
        if (!CheckStructuralHazard(second) || first->is_stalled) {
            second->is_stalled = true;
        }   
        else{
            second->cycles_remaining--;
        }
    }
    return;
}

void Simulation::AdvancePipeline() {
    // move instructions in reverse order

    // WriteBack instructions
    // statistics updated in WriteBack(), can move here later if needed
    pipeline.stages[WB].clear();

    // Memory Instructions
    // move instructions in memory stage to WriteBack - if possible
    while (pipeline.stages[MEM].size() > 0){
        Instruction* inst = pipeline.stages[MEM].front();
        if (!inst->is_stalled){             // instruction is not stalled
            pipeline.stages[MEM].erase(pipeline.stages[MEM].begin());
            pipeline.stages[WB].push_back(inst);
        } else  // if first instruction didn't leave MEM stage, Second instruction cannot 
            break;  // 
    }
    

    // move instruction in execute state to MEM - if possible
    // can only move instructions if MEM stage has empty slots
    for (size_t i = 0; i < 2 - pipeline.stages[MEM].size(); i++){
        Instruction* inst = pipeline.stages[EX].front();
        if (!inst->is_stalled ){
            inst->cycles_remaining = GetMEMCyclesCount(inst);
            inst->is_stalled = false;
            pipeline.stages[EX].erase(pipeline.stages[EX].begin());
            pipeline.stages[MEM].push_back(inst);
        }
        else{       // Two Load/Store instructions cannot enter MEM at same time -  handled in Execute()
            break;
        }
    }

    // move instruction in Decode to Execute - if possible
    // can only move instructions if Execute has empty slots
    for (size_t i = 0; i < 2 - pipeline.stages[EX].size(); i++){
        Instruction* inst = pipeline.stages[ID].front();
        if (!inst->is_stalled ){
            inst->cycles_remaining = GetEXCyclesCount(inst);
            inst->is_stalled = false;
            pipeline.stages[ID].erase(pipeline.stages[ID].begin());
            pipeline.stages[EX].push_back(inst);
        }
        else{
            break;
        }
    }

    // move instruction in IF to Decode - if possible
    // can only move instructions if Decode has empty slots 
    // no stalling in IF
    for (size_t i = 0; i < 2 - pipeline.stages[ID].size(); i++){
        Instruction* inst = pipeline.stages[IF].front();
        pipeline.stages[IF].erase(pipeline.stages[IF].begin());
        pipeline.stages[ID].push_back(inst);
    }
}

void Simulation::PrintStats() {

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
}
