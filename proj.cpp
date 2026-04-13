#include "proj.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>



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
            for (Instruction* active_inst : pipeline.stages[stage]) {
                // Stage and type-aware checking
                if (active_inst->dynamic_id == dep_id) {
                    // LOAD / STORE finished MEM check
                    if (active_inst->type == LOAD || active_inst->type == STORE) {
                        if (stage == EX || stage == MEM) return true;
                    } 
                    // INT / FP / BRANCH finished EX check
                    else {
                        if (stage == EX) return true;
                    }
                }
            }
        }
    }
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

void Simulation::Fetch() {
    int fetched_this_cycle = 0;

    while (fetched_this_cycle < 2 && fetch_index < instruction_window.size() && pipeline.stages[IF].size() < 2) {
        // Get the instruction to fetch
        Instruction* inst = instruction_window[fetch_index];
        // Place it into the IF stage of the pipeline and update the hash map with its dynamic ID 
        pipeline.stages[IF].push_back(inst);
        for (uint64_t dependency : inst->dependencies){
            if (latest_occurrence.count(dependency) > 0)
                inst->latest_occurence_dependency.push_back(latest_occurrence[dependency]);
        }
        latest_occurrence[inst->PC] = inst; // map update
        fetch_index++;
        fetched_this_cycle++;
        // If instruction is a branch instruciton - stall fetch
        // Set back to false when branch instruction moves out of EX in Advance Pipeline
        if (inst->type == BRANCH){
            fetch_stalled = true;
            break;
        }
    }
}

void Simulation::Decode() {
    if (pipeline.stages[ID].size() == 0) return;
    // For each instruction in Decode Stage check for hazards and determine if it can proceed to Execute.
    for (size_t i = 0; i < pipeline.stages[ID].size(); i++) {
        // pointer to the instruction being evaluated
        Instruction* inst = pipeline.stages[ID][i];
        // Start by assuming the instruction can move forward this cycle
        inst->is_stalled = false; 

        for (Instruction* dependecy_inst : inst->latest_occurence_dependency){
            if (dependecy_inst->completed == false){
                inst->is_stalled = true;
                break;
            }
        }
        if (inst->is_stalled)
            break;

    }
}

void Simulation::Execute() {
    // empty EX queue
    if (pipeline.stages[EX].size() == 0)
        return;

    // assume instruction is not stalled initially
    Instruction* first = pipeline.stages[EX][0];
    first->is_stalled = false;
    if (first->cycles_remaining > 0){    // incase instruction is done EX but waiting for empty slot in MEM
        first->cycles_remaining--;
    }

    // First instruction not finished all substages 
    if (first->cycles_remaining > 0)
        first->is_stalled = true;
    
    // 1 instruction in memory queue
    if (pipeline.stages[EX].size() == 1){
        return;
    }   // 2 or more Instructions in EX queueue
    
    // 2 or 3 Instructions in EX
    else{
        // MEM can be from size 2 - 4 if there are multiple LOAD instructions & D4 or D4
        for (size_t i = 1; i < pipeline.stages[EX].size(); i++){
            Instruction* next = pipeline.stages[EX][i];
            next->is_stalled = false;
            
            // check if cycles remaing is 0 - the case where non LOAD instruciton comes after a LOAD instruction
            if (next->cycles_remaining > 0)
                next->cycles_remaining--;

            // stall next instruciton if previous instruction is stalled OR cycles remaining is > 0
            if (pipeline.stages[EX][i-1]->is_stalled || next->cycles_remaining > 0)
                next->is_stalled = true;
        }
    }
    
}

void Simulation::Memory() {
    // empty memory queue
    if (pipeline.stages[MEM].size() == 0)
        return;

    // assume instruction is not stalled initially
    Instruction* first = pipeline.stages[MEM][0];
    first->is_stalled = false;
    // first MEM instruction will never be stalled, no need to check if remaining cycles is 0
    first->cycles_remaining--;

    // First instruction not finished all substages 
    if (first->cycles_remaining > 0)
        first->is_stalled = true;

    // 1 instruction in memory queue
    if (pipeline.stages[MEM].size() == 1){
        return;
    } // 2 or more instructions in memory queue
    else{
        // MEM can be from size 2 - 4 if there are multiple LOAD instructions & D4 or D4
        for (size_t i = 1; i < pipeline.stages[MEM].size(); i++){
            Instruction* next = pipeline.stages[MEM][i];
            next->is_stalled = false;
            
            // check if cycles remaing is 0 - the case where non LOAD instruciton comes after a LOAD instruction
            if (next->cycles_remaining > 0)
                next->cycles_remaining--;

            // stall next instruciton if previous instruction is stalled OR cycles remaining is > 0
            if (pipeline.stages[MEM][i-1]->is_stalled || next->cycles_remaining > 0)
                next->is_stalled = true;
        }
    }
    return;
}

void Simulation::WriteBack() {
    // statistic update done in Advance pipeline
    return;
}

void Simulation::AdvancePipeline() {
    // move instructions in reverse order

    // WriteBack -> Outside
    for (size_t i = 0; i < pipeline.stages[WB].size(); i++) {
        // edge case to prevent retired_instruction overflow
        if (retired_instructions == inst_count)
            break;

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
    }
    pipeline.stages[WB].clear();

    // Memory -> WriteBack
    // move up to 2 instructios in memory stage to WriteBack if not stalled
    // at most 2 instruction after Memory() will have is_stalled = false
    for (size_t i = 0; i < 2; i++){
        if (pipeline.stages[MEM].size() == 0)
            break;
        Instruction* inst = pipeline.stages[MEM].front();
        if (!inst->is_stalled){             // instruction is not stalled
            pipeline.stages[MEM].erase(pipeline.stages[MEM].begin());
            pipeline.stages[WB].push_back(inst);
            
            // update completed for dependencies
            if (inst->type == LOAD || inst->type == STORE)
                inst->completed = true;
        } else  // if first instruction didn't leave MEM stage, Second instruction cannot 
            break;  // 
    }
    

    // Execute -> Mem
    // Load instruction can always enter if MEM 1 free -> Second Load will always be rejected so MEM1 is essentially always free
    // Non-Load instruction can enter if MEM.size is 0 or 1 OR all instructions inside are Load Instructions (see edge case below)


    for (size_t i = 0; i < 2; i++){
        if (pipeline.stages[EX].empty() || (pipeline.stages[MEM].size() >= 2 && (D == 1 || D == 2)) || pipeline.stages[MEM].size() >= 4) break;
        Instruction* inst = pipeline.stages[EX].front();
        
        if (inst->is_stalled )
            break;
            // incoming is a Floating Point instruciton & last instruction in EX is Load in EX2
            // or EX queue of size 0 or 1          
            // or First and Last instructions are Floati instructions && incoming instruciton is a non-load Instruction in D3 of D4



        bool LOAD_MEM1_exists = false;
        bool STORE_exists = false;
        bool Load_exists = false;
        int non_LOAD_count = 0;
        for (size_t j = 0; j < pipeline.stages[MEM].size(); j++){
            if (pipeline.stages[MEM][j]->type == STORE){
                STORE_exists = true;
                non_LOAD_count++;
            }
            else if (pipeline.stages[MEM][j]->type == LOAD){
                Load_exists = true;
                if (pipeline.stages[MEM][j]->cycles_remaining == GetMEMCyclesCount(pipeline.stages[MEM][j]))
                    LOAD_MEM1_exists = true;
            }
            else
                non_LOAD_count++;
        }

        if (inst->type == STORE && STORE_exists) break;
        if (inst->type != LOAD){
            if (Load_exists && non_LOAD_count >= 1) break;
            if (!Load_exists && non_LOAD_count >= 2) break;
        }
        if (inst->type == LOAD && LOAD_MEM1_exists) break;


        if (inst->type == INT || inst->type == FP || inst->type == BRANCH) {
            inst->completed = true;
        }

        inst->cycles_remaining = GetMEMCyclesCount(inst);
        inst->is_stalled = false;
        pipeline.stages[EX].erase(pipeline.stages[EX].begin());
        pipeline.stages[MEM].push_back(inst);

        if (inst->type == BRANCH)
            fetch_stalled = false;
    }




    // Decode -> Execute
    // Prevents Double Integer/Floating Point/Branch Going into EX at same time
    // Floating Point can always enter if EX 1 free
    // Non-Load instruction can enter if EX.size is 0 or 1 OR all instructions are Floating point

    for (size_t i = 0; i < 2; i++){
        if (pipeline.stages[ID].empty() || (pipeline.stages[EX].size() >= 2 && (D == 1 || D == 3)) || pipeline.stages[EX].size() >= 3) break;
        Instruction* inst = pipeline.stages[ID].front();
        
        if (inst->is_stalled )
            break;
            // incoming is a Floating Point instruciton & last instruction in EX is Load in EX2
            // or EX queue of size 0 or 1          
            // or First and Last instructions are Floati instructions && incoming instruciton is a non-load Instruction in D3 of D4



        bool FP_EX1_exists = false;
        bool FP_exists = false;
        bool INT_exists = false;
        bool BRANCH_exists = false;
        int non_FP_count = 0;
        for (size_t j = 0; j < pipeline.stages[EX].size(); j++){
            if (pipeline.stages[EX][j]->type == INT){
                INT_exists = true;
                non_FP_count++;
            }
            else if (pipeline.stages[EX][j]->type == BRANCH){
                BRANCH_exists = true;
                non_FP_count++;
            }
            else if (pipeline.stages[EX][j]->type == FP){
                FP_exists = true;
                if (pipeline.stages[EX][j]->cycles_remaining == GetEXCyclesCount(pipeline.stages[EX][j]))
                    FP_EX1_exists = true;
            }
            else
                non_FP_count++;
        }

        if (inst->type == INT && INT_exists) break;
        if (inst->type == BRANCH && BRANCH_exists) break;
        if (inst->type == FP && FP_EX1_exists) break;
        if (inst->type != FP){
            if (FP_exists && non_FP_count >= 1) break;
            if (!FP_exists && non_FP_count >= 2) break;
        }

        inst->cycles_remaining = GetEXCyclesCount(inst);
        inst->is_stalled = false;
        pipeline.stages[ID].erase(pipeline.stages[ID].begin());
        pipeline.stages[EX].push_back(inst);
    }

    // Fetch -> Decode
    // can only move instructions if Decode has empty slots 
    // no stalling in IF
    for (size_t i = 0; i < 2 ; i++){
        if (pipeline.stages[IF].empty() || pipeline.stages[ID].size() == 2) break;
        Instruction* inst = pipeline.stages[IF].front();
        pipeline.stages[IF].erase(pipeline.stages[IF].begin());
        pipeline.stages[ID].push_back(inst);
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
        printf("Usage Error. Correct Usage: ./sim <trace_file> <start_inst> <inst_count> <depth_config>\n");
        printf("Terminating Simulation...\n");
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
