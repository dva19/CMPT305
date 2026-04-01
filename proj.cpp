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
    std::ifstream file(trace_file_name);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open trace file\n";
        return;
    }

    std::string line;
    uint64_t current_line_num = 0;
    uint64_t loaded_count = 0;
    uint64_t dynamic_id_counter = 1;

    std::cout << "Loading instructions from " << trace_file_name << "...\n";

    while (std::getline(file, line)) {
        current_line_num++;

        if (current_line_num < start_inst) continue; 
        if (loaded_count >= inst_count) break;

        std::stringstream ss(line);
        std::string token;

        std::getline(ss, token, ',');
        uint64_t pc = std::stoull(token, nullptr, 16);

        std::getline(ss, token, ',');
        InstructionType type = static_cast<InstructionType>(std::stoi(token));

        Instruction* inst = new Instruction(pc, type, dynamic_id_counter++);

        while (std::getline(ss, token, ',')) {
            if (!token.empty() && token.back() == '\r') token.pop_back(); 
            if (!token.empty()) {
                inst->dependencies.push_back(std::stoull(token, nullptr, 16));
            }
        }

        instruction_window.push_back(inst);
        loaded_count++;
    }

    file.close();
    std::cout << "Successfully loaded " << instruction_window.size() << " instructions.\n\n";
}

int Simulation::GetEXCyclesCount(Instruction* inst) {
    if (inst->type == FP && (D == 2 || D == 4)) return 2;
    return 1;
}

int Simulation::GetMEMCyclesCount(Instruction* inst) {
    if (inst->type == LOAD && (D == 3 || D == 4)) return 3;
    return 1;
}

bool Simulation::CheckDataHazard(Instruction* inst) { 
    for (uint64_t dep_id : inst->dependencies) {
        for (int stage = EX; stage <= WB; stage++) {
            for (Instruction* active_inst : pipeline.stages[stage]) {
                if (active_inst->dynamic_id == dep_id) return true; 
            }
        }
    }
    return false; 
}

bool Simulation::CheckStructuralHazard(Instruction* inst) { 
    switch (inst->type) {
        case INT:    return resources.int_inUse;
        case FP:     return resources.fp_inUse;
        case BRANCH: return resources.branch_inUse;
        case LOAD:   return resources.mem_read_inUse;
        case STORE:  return resources.mem_write_inUse;
        default:     return false;
    }
}

void Simulation::Decode() {
    for (size_t i = 0; i < pipeline.stages[ID].size(); i++) {
        Instruction* inst = pipeline.stages[ID][i];
        inst->is_stalled = false; 

        if (!inst->dependencies_satisfied) {
            vector<uint64_t> translated_dynamic_ids;
            for (uint64_t static_pc : inst->dependencies) {
                if (latest_occurrence.count(static_pc) > 0) {
                    translated_dynamic_ids.push_back(latest_occurrence[static_pc]);
                }
            }
            inst->dependencies = translated_dynamic_ids;
            inst->dependencies_satisfied = true; 
        }

        bool data_hazard = CheckDataHazard(inst);
        bool structural_hazard = CheckStructuralHazard(inst);

        if (data_hazard || structural_hazard) {
            inst->is_stalled = true; 
            // in-order stall: block the one behind too
            if (i + 1 < pipeline.stages[ID].size()) {
                pipeline.stages[ID][i+1]->is_stalled = true;
            }
            break; 
        } else {
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
        Instruction* inst = instruction_window[fetch_index];
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
    if (argc != 5) {
        std::cout << "Usage: ./sim <trace_file> <start_inst> <inst_count> <depth_config>\n";
        return 1;
    }

    Simulation sim(argv[1], std::stoull(argv[2]), std::stoull(argv[3]), std::stoi(argv[4]));
    sim.LoadInstructions();
    sim.RunSimulation();
    sim.PrintStats();

    return 0;
}

void Simulation::WriteBack() {
   
}

void Simulation::Memory() {
    
}

void Simulation::Execute() {
    
}

void Simulation::AdvancePipeline() {
    
}

void Simulation::PrintStats() {
    
}