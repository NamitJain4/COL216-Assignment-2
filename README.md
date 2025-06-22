# RISC-V Pipelined Processor Simulator

## Overview
This project implements a cycle-accurate simulator for a pipelined RISC-V processor with support for data forwarding. The simulator models the five classic pipeline stages (IF, ID, EX, MEM, WB) and handles various hazards that occur in pipelined architectures.

## Features
- Complete implementation of RV32I instruction set
- Five-stage pipeline (Instruction Fetch, Decode, Execute, Memory, Write Back)
- Data forwarding capability to reduce pipeline stalls
- Support for control hazards (branches, jumps)
- reads the instructions from input txt files
- pipeline execution visualization stored in txt files

## Implementation Details

### Pipeline Stages
1. **Instruction Fetch (IF)**: Fetches the next instruction from memory
2. **Instruction Decode (ID)**: Decodes the instruction and reads register values
3. **Execute (EX)**: Performs ALU operations
4. **Memory (MEM)**: Accesses data memory for loads and stores
5. **Write Back (WB)**: Writes results back to register file

### Hazard Handling
- **Data Hazards**: Resolved through forwarding or stalling
- **Control Hazards**: Handled by early branch resolution in the ID stage
- **Structural Hazards**: Not explicitly modeled. Because we are already constraining it to use a pipeline stage by only one instruction

## Challenges Faced

### Branch Handling and Forwarding
One of the most significant challenges was properly implementing forwarding for branch instructions. Since branches are evaluated in the ID stage, they require special handling. The main issue was that when an instruction directly preceding a branch modifies a register that the branch depends on, the updated value isn't available yet.

For example, in this sequence:

addi x6, x6, 1 beq x6, x0, label

Since the `addi` instruction modifies `x6` and the very next instruction needs to read `x6`, the pipeline needs to stall for one cycle to get the correct value. This required implementing additional logic in the ID stage to detect this specific hazard.

### Pipeline Visualization
Creating an accurate and useful pipeline visualization system was challenging. The implementation needed to track which stages each instruction occupies in each cycle, which becomes complex with stalls and flushes.

### Instruction Decoding
Properly decoding and implementing the complete RV32I instruction set required careful attention to detail. The bit-manipulation for different instruction formats (R-type, I-type, S-type, B-type, U-type, J-type) was particularly tricky.


## Analysis

With forwarding enabled, the processor achieves better performance by reducing stalls due to data dependencies. However, certain hazards still require stalls, such as when load instructions are followed immediately by instructions that use the loaded value, or when branches depend on the result of the immediately preceding instruction.

The implementation demonstrates the trade-offs between hardware complexity (forwarding paths) and performance improvements.
