#include "proj.h"
#include <iostream>

void Simulation::LoadInstructions() {
}

void Simulation::AdvancePipeline() {
}

bool Simulation::CheckDataHazard(Instruction* inst) {
    return false;
}

bool Simulation::CheckStructuralHazard(Instruction* inst) {
    return false;
}

int Simulation::GetEXCyclesCount(Instruction* inst) {
    return 1;
}

int Simulation::GetMEMCyclesCount(Instruction* inst) {
    return 1;
}

void Simulation::Fetch() {
}

void Simulation::Decode() {
}

void Simulation::Execute() {
}

void Simulation::Memory() {
}

void Simulation::WriteBack() {
}

void Simulation::RunSimulation() {
}

int main(int argc, char* argv[]){
    return 0;
}