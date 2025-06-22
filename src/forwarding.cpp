#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <map>

enum InstructionFormat {
    R_TYPE, I_TYPE, S_TYPE, B_TYPE, U_TYPE, J_TYPE
};

enum Opcode {
    // R-type
    ADD = 0, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND,
    // I-type
    ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI, LB, LH, LW, LBU, LHU,
    // S-type
    SB, SH, SW,
    // B-type
    BEQ, BNE, BLT, BGE, BLTU, BGEU,
    // U-type
    LUI, AUIPC,
    // J-type
    JAL, JALR,
    // Invalid
    INVALID
};

const std::map<Opcode, std::string> opcodeToString = {
    {ADD, "add"}, {SUB, "sub"}, {SLL, "sll"}, {SLT, "slt"}, {SLTU, "sltu"}, 
    {XOR, "xor"}, {SRL, "srl"}, {SRA, "sra"}, {OR, "or"}, {AND, "and"},
    {ADDI, "addi"}, {SLTI, "slti"}, {SLTIU, "sltiu"}, {XORI, "xori"}, 
    {ORI, "ori"}, {ANDI, "andi"}, {SLLI, "slli"}, {SRLI, "srli"}, {SRAI, "srai"},
    {LB, "lb"}, {LH, "lh"}, {LW, "lw"}, {LBU, "lbu"}, {LHU, "lhu"},
    {SB, "sb"}, {SH, "sh"}, {SW, "sw"},
    {BEQ, "beq"}, {BNE, "bne"}, {BLT, "blt"}, {BGE, "bge"}, {BLTU, "bgeu"}, {BGEU, "bgeu"},
    {LUI, "lui"}, {AUIPC, "auipc"},
    {JAL, "jal"}, {JALR, "jalr"}
};

struct Instruction {
    uint32_t raw;
    Opcode opcode;
    InstructionFormat format;
    int rs1, rs2, rd;
    int32_t immediate;
    
    Instruction() : raw(0), opcode(INVALID), format(R_TYPE), rs1(-1), rs2(-1), rd(-1), immediate(0) {}
};

struct ControlSignals {
    bool regWrite, memRead, memWrite, memToReg, aluSrc, branch, jump;
    int aluOp;
    
    ControlSignals() : regWrite(false), memRead(false), memWrite(false), 
                        memToReg(false), aluSrc(false), branch(false), 
                        jump(false), aluOp(0) {}
};

struct ALUResult {
    int32_t result;
    bool zero, negative, overflow;
    
    ALUResult() : result(0), zero(false), negative(false), overflow(false) {}
};

struct IF_ID_Register {
    uint32_t pc;
    Instruction instruction;
    bool valid;
    
    IF_ID_Register() : pc(0), valid(false) {}
};

struct ID_EX_Register {
    uint32_t pc;
    Instruction instruction;
    int32_t readData1, readData2, immediate;
    ControlSignals control;
    bool valid;
    
    ID_EX_Register() : pc(0), readData1(0), readData2(0), immediate(0), valid(false) {}
};

struct EX_MEM_Register {
    uint32_t pc, branchTarget;
    Instruction instruction;
    ALUResult aluResult;
    int32_t readData2;
    ControlSignals control;
    bool branchTaken, valid;
    
    EX_MEM_Register() : pc(0), branchTarget(0), readData2(0), 
                    branchTaken(false), valid(false) {}
};

struct MEM_WB_Register {
    uint32_t pc;
    Instruction instruction;
    int32_t aluResult, readData;
    ControlSignals control;
    bool valid;
    
    MEM_WB_Register() : pc(0), aluResult(0), readData(0), valid(false) {}
};

struct InstructionMemory {
    std::vector<uint32_t> memory;
    
    uint32_t readInstruction(uint32_t address) {
        if (address / 4 < memory.size()) return memory[address / 4];
        return 0;
    }
};

struct RegisterFile {
    int32_t registers[32];
    
    RegisterFile() {
        for (int i = 0; i < 32; i++) registers[i] = 0;
    }
    
    int32_t read(int reg) {
        if (reg >= 0 && reg < 32) return registers[reg];
        return 0;
    }
    
    void write(int reg, int32_t value) {
        if (reg > 0 && reg < 32) registers[reg] = value;
    }
};

struct DataMemory {
    std::vector<uint8_t> memory;
    
    DataMemory(size_t size = 1024) { memory.resize(size, 0); }
    
    int32_t read(uint32_t address, int size) {
        if (address + size - 1 < memory.size()) {
            int32_t value = 0;
            for (int i = 0; i < size; i++) {
                value |= (static_cast<int32_t>(memory[address + i]) << (i * 8));
            }
            return value;
        }
        return 0;
    }
    
    void write(uint32_t address, int32_t value, int size) {
        if (address + size - 1 < memory.size()) {
            for (int i = 0; i < size; i++) {
                memory[address + i] = (value >> (i * 8)) & 0xFF;
            }
        }
    }
};

struct ForwardingUnit {
    enum ForwardSource {
        FROM_REG = 0, FROM_EX_MEM, FROM_MEM_WB
    };
    
    ForwardSource forwardA, forwardB;
    
    ForwardingUnit() : forwardA(FROM_REG), forwardB(FROM_REG) {}
    
    void detectForwarding(const ID_EX_Register& idEx,
                         const EX_MEM_Register& exMem,
                         const MEM_WB_Register& memWb) {
        forwardA = FROM_REG;
        forwardB = FROM_REG;
        
        if (!idEx.valid) return;
        
        if (idEx.instruction.rs1 != 0) {
            if (exMem.valid && exMem.control.regWrite && 
                exMem.instruction.rd != 0 && 
                exMem.instruction.rd == idEx.instruction.rs1) {
                forwardA = FROM_EX_MEM;
            } else if (memWb.valid && memWb.control.regWrite && 
                    memWb.instruction.rd != 0 && 
                    memWb.instruction.rd == idEx.instruction.rs1) {
                forwardA = FROM_MEM_WB;
            }
        }
        
        if (idEx.instruction.rs2 != 0) {
            if (exMem.valid && exMem.control.regWrite && 
                exMem.instruction.rd != 0 && 
                exMem.instruction.rd == idEx.instruction.rs2) {
                forwardB = FROM_EX_MEM;
            } else if (memWb.valid && memWb.control.regWrite && 
                    memWb.instruction.rd != 0 && 
                    memWb.instruction.rd == idEx.instruction.rs2) {
                forwardB = FROM_MEM_WB;
            }
        }
    }
};

struct HazardDetectionUnit {
    bool detectHazardF(const IF_ID_Register& ifId, const ID_EX_Register& idEx, 
                     const EX_MEM_Register& exMem, const MEM_WB_Register& memWb, 
                     bool isForwarding, bool isIFStage = false) {
        if (!ifId.valid) return false;
        
        int rs1 = ifId.instruction.rs1;
        int rs2 = ifId.instruction.rs2;
        
        bool usesRs1 = rs1 != 0 && 
                      (ifId.instruction.format != U_TYPE && 
                       ifId.instruction.format != J_TYPE);
                        
        bool usesRs2 = rs2 != 0 && 
                      (ifId.instruction.format == R_TYPE || 
                       ifId.instruction.format == B_TYPE || 
                       ifId.instruction.format == S_TYPE);
        
        bool isBranchOrJump = (ifId.instruction.format == B_TYPE || 
                              ifId.instruction.format == J_TYPE ||
                              (ifId.instruction.format == I_TYPE && ifId.instruction.opcode == JALR));
        
        if (isIFStage && !isBranchOrJump) return false;
        
        if (isForwarding) {
            if ((idEx.valid && idEx.control.memRead && idEx.instruction.rd != 0)) {
                if ((usesRs1 && rs1 == idEx.instruction.rd) || 
                    (usesRs2 && rs2 == idEx.instruction.rd)) {
                    return true;
                }
            }
            if ((isBranchOrJump && memWb.valid && memWb.control.memRead && memWb.instruction.rd != 0)) {
                if ((usesRs1 && rs1 == memWb.instruction.rd) || 
                    (usesRs2 && rs2 == memWb.instruction.rd)) {
                    return true;
                }
            }
            return false;
        }
        
        if (idEx.valid && idEx.control.regWrite && idEx.instruction.rd != 0) {
            if ((usesRs1 && rs1 == idEx.instruction.rd) || 
                (usesRs2 && rs2 == idEx.instruction.rd)) {
                return true;
            }
        }
        
        if (exMem.valid && exMem.control.regWrite && exMem.instruction.rd != 0) {
            if ((usesRs1 && rs1 == exMem.instruction.rd) || 
                (usesRs2 && rs2 == exMem.instruction.rd)) {
                return true;
            }
        }
        
        if (memWb.valid && memWb.control.regWrite && memWb.instruction.rd != 0) {
            if ((usesRs1 && rs1 == memWb.instruction.rd) || 
                (usesRs2 && rs2 == memWb.instruction.rd)) {
                if (!isIFStage) return true;
            }
        }
        
        return false;
    }
};

struct Processor {
    uint32_t pc;
    InstructionMemory instMem;
    RegisterFile regFile;
    DataMemory dataMem;
    HazardDetectionUnit hazardUnit;
    ForwardingUnit forwardUnit;
    
    IF_ID_Register ifId;
    ID_EX_Register idEx;
    EX_MEM_Register exMem;
    MEM_WB_Register memWb;
    
    int clockCycle, instructionsExecuted;
    std::ofstream traceFile;
    std::ofstream outputFile;
    
    struct InstructionTrace {
        uint32_t address;
        uint32_t raw;
        std::string disassembly;
        std::vector<std::string> stages;
    };
    
    std::vector<InstructionTrace> instructionTraces;
    
    Processor() : pc(0), clockCycle(0), instructionsExecuted(0) {}
    
    void reset() {
        pc = 0;
        clockCycle = 0;
        instructionsExecuted = 0;
        ifId = IF_ID_Register();
        idEx = ID_EX_Register();
        exMem = EX_MEM_Register();
        memWb = MEM_WB_Register();
    }
    
    void openTraceFile(const std::string& filename) {
        traceFile.open(filename);
        if (!traceFile) std::cerr << "Error opening trace file: " << filename << std::endl;
    }

    void openOutputFile(const std::string& filename) {
        outputFile.open(filename);
        if (!outputFile) std::cerr << "Error opening trace file: " << filename << std::endl;
    }
    
    void closeTraceFile() {
        if (traceFile.is_open()) traceFile.close();
    }

    void closeOutputFile() {
        if (outputFile.is_open()) outputFile.close();
    }

    void initInstructionTrace(uint32_t pc, uint32_t raw) {
        for (size_t i = 0; i < instructionTraces.size(); i++) {
            if (instructionTraces[i].address == pc) return;
        }
        
        InstructionTrace trace;
        trace.address = pc;
        trace.raw = raw;
        
        Instruction inst;
        decodeInstruction(raw, inst);
        if (inst.opcode != INVALID) {
            std::string opName = opcodeToString.at(inst.opcode);
            std::string regNames = "";
            
            if (inst.format == R_TYPE) {
                regNames = " x" + std::to_string(inst.rd) + ",x" + std::to_string(inst.rs1) + ",x" + std::to_string(inst.rs2);
            } else if (inst.format == I_TYPE) {
                regNames = " x" + std::to_string(inst.rd) + ",x" + std::to_string(inst.rs1) + "," + std::to_string(inst.immediate);
            } else if (inst.format == J_TYPE) {
                regNames = " x" + std::to_string(inst.rd) + "," + std::to_string(inst.immediate);
            } else if (inst.format == B_TYPE) {
                regNames = " x" + std::to_string(inst.rs1) + ",x" + std::to_string(inst.rs2) + "," + std::to_string(inst.immediate);
            }
            
            trace.disassembly = opName + regNames;
        } else {
            trace.disassembly = "unknown";
        }
        
        instructionTraces.push_back(trace);
    }
    
    void trackInstructionStage(int instructionIndex, int cycle, const std::string& stage) {
        if (instructionIndex >= 0 && static_cast<size_t>(instructionIndex) < instructionTraces.size()) {
            if (instructionTraces[instructionIndex].stages.size() <= static_cast<size_t>(cycle)) {
                instructionTraces[instructionIndex].stages.resize(cycle + 1, "-");
            }
            instructionTraces[instructionIndex].stages[cycle] = stage;
        }
    }
    
    void outputPipelineTraceCSV() {
        if (!traceFile.is_open()) return;
        
        traceFile << "PC,Instruction,";
        for (int i = 1; i <= clockCycle; i++) {
            traceFile << "Cycle " << i;
            if (i < clockCycle) traceFile << ",";
        }
        traceFile << std::endl;
        
        for (const auto& trace : instructionTraces) {
            traceFile << std::hex << "0x" << trace.address << "," 
                      << trace.disassembly << ",";
            
            for (int i = 0; i < clockCycle; i++) {
                if (static_cast<size_t>(i) < trace.stages.size()) {
                    traceFile << trace.stages[i];
                } else {
                    traceFile << "-";
                }
                if (i < clockCycle - 1) traceFile << ",";
            }
            traceFile << std::endl;
        }
    }

    void outputPipelineTraceTXT() {
        if (!outputFile.is_open()) return;
                
        for (const auto& trace : instructionTraces) {
            outputFile << trace.disassembly << ";";
            
            for (int i = 0; i < clockCycle; i++) {
                if (static_cast<size_t>(i) < trace.stages.size()) {
                    outputFile << trace.stages[i];
                } else {
                    outputFile << "-";
                }
                if (i < clockCycle - 1) outputFile << ";";
            }
            outputFile << std::endl;
        }
    }
    
    void printTraceHeader() {
        if (traceFile.is_open()) {
            traceFile << "Cycle\tPC\tInstruction\tIF/ID.IR\tID/EX.IR\tEX/MEM.IR\tMEM/WB.IR\n";
        }
    }
    
    void printTrace() {
        if (traceFile.is_open()) {
            traceFile << clockCycle << "\t";
            traceFile << std::hex << pc << "\t";
            
            uint32_t currentInst = instMem.readInstruction(pc);
            traceFile << std::hex << currentInst << "\t";
            
            traceFile << std::hex << (ifId.valid ? ifId.instruction.raw : 0) << "\t";
            traceFile << std::hex << (idEx.valid ? idEx.instruction.raw : 0) << "\t";
            traceFile << std::hex << (exMem.valid ? exMem.instruction.raw : 0) << "\t";
            traceFile << std::hex << (memWb.valid ? memWb.instruction.raw : 0) << "\n";
        }
    }
    
    void decodeInstruction(uint32_t rawInst, Instruction& inst) {
        inst.raw = rawInst;
        uint32_t opcodeField = rawInst & 0x7F;
        
        switch (opcodeField) {
            case 0x33: { // R-type
                inst.format = R_TYPE;
                inst.rd = (rawInst >> 7) & 0x1F;
                inst.rs1 = (rawInst >> 15) & 0x1F;
                inst.rs2 = (rawInst >> 20) & 0x1F;
                
                uint32_t funct3 = (rawInst >> 12) & 0x7;
                uint32_t funct7 = (rawInst >> 25) & 0x7F;
                
                if (funct7 == 0x00) {
                    switch (funct3) {
                        case 0x0: inst.opcode = ADD; break;
                        case 0x1: inst.opcode = SLL; break;
                        case 0x2: inst.opcode = SLT; break;
                        case 0x3: inst.opcode = SLTU; break;
                        case 0x4: inst.opcode = XOR; break;
                        case 0x5: inst.opcode = SRL; break;
                        case 0x6: inst.opcode = OR; break;
                        case 0x7: inst.opcode = AND; break;
                        default: inst.opcode = INVALID;
                    }
                } else if (funct7 == 0x20) {
                    switch (funct3) {
                        case 0x0: inst.opcode = SUB; break;
                        case 0x5: inst.opcode = SRA; break;
                        default: inst.opcode = INVALID;
                    }
                } else {
                    inst.opcode = INVALID;
                }
                break;
            }
                
            case 0x13: { // I-type (immediate)
                inst.format = I_TYPE;
                inst.rd = (rawInst >> 7) & 0x1F;
                inst.rs1 = (rawInst >> 15) & 0x1F;
                inst.immediate = (rawInst >> 20);
                if (inst.immediate & 0x800) inst.immediate |= 0xFFFFF000;
                
                uint32_t funct3_i = (rawInst >> 12) & 0x7;
                
                switch (funct3_i) {
                    case 0x0: inst.opcode = ADDI; break;
                    case 0x2: inst.opcode = SLTI; break;
                    case 0x3: inst.opcode = SLTIU; break;
                    case 0x4: inst.opcode = XORI; break;
                    case 0x6: inst.opcode = ORI; break;
                    case 0x7: inst.opcode = ANDI; break;
                    case 0x1: inst.opcode = SLLI; break;
                    case 0x5:
                        if (((rawInst >> 25) & 0x7F) == 0x00) {
                            inst.opcode = SRLI;
                        } else if (((rawInst >> 25) & 0x7F) == 0x20) {
                            inst.opcode = SRAI;
                        } else {
                            inst.opcode = INVALID;
                        }
                        break;
                    default: inst.opcode = INVALID;
                }
                break;
            }
                
            case 0x03: { // I-type (loads)
                inst.format = I_TYPE;
                inst.rd = (rawInst >> 7) & 0x1F;
                inst.rs1 = (rawInst >> 15) & 0x1F;
                inst.immediate = (rawInst >> 20);
                if (inst.immediate & 0x800) inst.immediate |= 0xFFFFF000;
                
                uint32_t funct3_l = (rawInst >> 12) & 0x7;
                
                switch (funct3_l) {
                    case 0x0: inst.opcode = LB; break;
                    case 0x1: inst.opcode = LH; break;
                    case 0x2: inst.opcode = LW; break;
                    case 0x4: inst.opcode = LBU; break;
                    case 0x5: inst.opcode = LHU; break;
                    default: inst.opcode = INVALID;
                }
                break;
            }
                
            case 0x23: { // S-type
                inst.format = S_TYPE;
                inst.rs1 = (rawInst >> 15) & 0x1F;
                inst.rs2 = (rawInst >> 20) & 0x1F;
                inst.immediate = ((rawInst >> 25) & 0x7F) << 5;
                inst.immediate |= (rawInst >> 7) & 0x1F;
                if (inst.immediate & 0x800) inst.immediate |= 0xFFFFF000;
                
                uint32_t funct3_s = (rawInst >> 12) & 0x7;
                
                switch (funct3_s) {
                    case 0x0: inst.opcode = SB; break;
                    case 0x1: inst.opcode = SH; break;
                    case 0x2: inst.opcode = SW; break;
                    default: inst.opcode = INVALID;
                }
                break;
            }
                
            case 0x63: { // B-type
                inst.format = B_TYPE;
                inst.rs1 = (rawInst >> 15) & 0x1F;
                inst.rs2 = (rawInst >> 20) & 0x1F;
                inst.immediate = 0;
                inst.immediate |= ((rawInst >> 31) & 0x1) << 12;
                inst.immediate |= ((rawInst >> 7) & 0x1) << 11;
                inst.immediate |= ((rawInst >> 25) & 0x3F) << 5;
                inst.immediate |= ((rawInst >> 8) & 0xF) << 1;
                if (inst.immediate & 0x1000) inst.immediate |= 0xFFFFE000;
                
                uint32_t funct3_b = (rawInst >> 12) & 0x7;
                
                switch (funct3_b) {
                    case 0x0: inst.opcode = BEQ; break;
                    case 0x1: inst.opcode = BNE; break;
                    case 0x4: inst.opcode = BLT; break;
                    case 0x5: inst.opcode = BGE; break;
                    case 0x6: inst.opcode = BLTU; break;
                    case 0x7: inst.opcode = BGEU; break;
                    default: inst.opcode = INVALID;
                }
                break;
            }
                
            case 0x37: // U-type (LUI)
                inst.format = U_TYPE;
                inst.rd = (rawInst >> 7) & 0x1F;
                inst.immediate = rawInst & 0xFFFFF000;
                inst.opcode = LUI;
                break;
                
            case 0x17: // U-type (AUIPC)
                inst.format = U_TYPE;
                inst.rd = (rawInst >> 7) & 0x1F;
                inst.immediate = rawInst & 0xFFFFF000;
                inst.opcode = AUIPC;
                break;
                
            case 0x6F: { // J-type (JAL)
                inst.format = J_TYPE;
                inst.rd = (rawInst >> 7) & 0x1F;
                inst.immediate = 0;
                inst.immediate |= ((rawInst >> 31) & 0x1) << 20;
                inst.immediate |= ((rawInst >> 12) & 0xFF) << 12;
                inst.immediate |= ((rawInst >> 20) & 0x1) << 11;
                inst.immediate |= ((rawInst >> 21) & 0x3FF) << 1;
                if (inst.immediate & 0x100000) inst.immediate |= 0xFFF00000;
                inst.opcode = JAL;
                break;
            }
                
            case 0x67: { // I-type (JALR)
                inst.format = I_TYPE;
                inst.rd = (rawInst >> 7) & 0x1F;
                inst.rs1 = (rawInst >> 15) & 0x1F;
                inst.immediate = (rawInst >> 20);
                if (inst.immediate & 0x800) inst.immediate |= 0xFFFFF000;
                inst.opcode = JALR;
                break;
            }
                
            default:
                inst.opcode = INVALID;
                break;
        }
    }
    
    void setControlSignals(const Instruction& inst, ControlSignals& control) {
        control = ControlSignals();
        
        switch (inst.format) {
            case R_TYPE:
                control.regWrite = true;
                control.aluOp = 2;
                break;
                
            case I_TYPE:
                if (inst.opcode == JALR) {
                    control.regWrite = true;
                    control.jump = true;
                    control.aluOp = 0;
                } else if (inst.opcode >= LB && inst.opcode <= LHU) {
                    control.regWrite = true;
                    control.memRead = true;
                    control.memToReg = true;
                    control.aluSrc = true;
                    control.aluOp = 0;
                } else {
                    control.regWrite = true;
                    control.aluSrc = true;
                    control.aluOp = 3;
                }
                break;
                
            case S_TYPE:
                control.memWrite = true;
                control.aluSrc = true;
                control.aluOp = 0;
                break;
                
            case B_TYPE:
                control.branch = true;
                control.aluOp = 1;
                break;
                
            case U_TYPE:
                control.regWrite = true;
                if (inst.opcode == AUIPC) {
                    control.aluOp = 0;
                    control.aluSrc = true;
                } else {
                    control.aluOp = 4;
                    control.aluSrc = true;
                }
                break;
                
            case J_TYPE:
                control.regWrite = true;
                control.jump = true;
                break;
        }
    }
    
    void printTerminalTrace() {
        std::cout << "+-----------+-----------------+";
        for (int i = 1; i <= clockCycle; i++) std::cout << "-----+";
        std::cout << "\n";
        
        std::cout << "| PC        |   Instruction   |";
        for (int i = 1; i <= clockCycle; i++) std::cout << " C" << std::setw(2) << i << " |";
        std::cout << "\n";
        
        std::cout << "+-----------+-----------------+";
        for (int i = 1; i <= clockCycle; i++) std::cout << "-----+";
        std::cout << "\n";
        
        for (const auto& trace : instructionTraces) {
            std::cout << "| 0x" << std::hex << std::setw(8) << std::left << trace.address 
                       << "| " << std::setw(15) << std::left << trace.disassembly << " |";
            
            for (int i = 0; i < clockCycle; i++) {
                std::string stage = "-";
                if (static_cast<size_t>(i) < trace.stages.size()) stage = trace.stages[i];
                std::cout << " " << std::setw(3) << std::left << stage << " |";
            }
            std::cout << "\n";
        }
        
        std::cout << "+-----------+-----------------+";
        for (int i = 1; i <= clockCycle; i++) std::cout << "-----+";
        std::cout << "\n" << std::dec;
    }
};

void instructionFetchStage(Processor& cpu, bool& stall, bool isForwarding = false) {
    if (stall) return;
    
    uint32_t instruction = cpu.instMem.readInstruction(cpu.pc);
    
    int instIndex = -1;
    for (size_t i = 0; i < cpu.instructionTraces.size(); i++) {
        if (cpu.instructionTraces[i].address == cpu.pc) {
            instIndex = i;
            break;
        }
    }
    
    if (instIndex >= 0)
        cpu.trackInstructionStage(instIndex, cpu.clockCycle - 1, "IF");
    else {
        cpu.ifId.valid = false;
        return;
    }
    
    cpu.ifId.pc = cpu.pc;
    cpu.decodeInstruction(instruction, cpu.ifId.instruction);
    cpu.ifId.valid = true;
    
    cpu.pc += 4;
    
    IF_ID_Register tempIfId = cpu.ifId;
    bool ifStall = cpu.hazardUnit.detectHazardF(tempIfId, cpu.idEx, cpu.exMem, cpu.memWb, isForwarding, true);
    if (ifStall) stall = true;
}

void instructionDecodeStage(Processor& cpu, bool& stall, bool& branchTaken, uint32_t& branchTarget, bool isForwarding = false) {
    branchTaken = false;
    branchTarget = 0;
    
    if (!cpu.ifId.valid) {
        cpu.idEx.valid = false;
        return;
    }
    
    int instIndex = -1;
    for (size_t i = 0; i < cpu.instructionTraces.size(); i++) {
        if (cpu.instructionTraces[i].address == cpu.ifId.pc) {
            instIndex = i;
            break;
        }
    }
    
    bool isStalled = cpu.hazardUnit.detectHazardF(cpu.ifId, cpu.idEx, cpu.exMem, cpu.memWb, isForwarding, false);
    stall = isStalled;
    
    if (instIndex >= 0) cpu.trackInstructionStage(instIndex, cpu.clockCycle - 1, "ID");
    
    if (isStalled) {
        uint32_t nextPC = cpu.pc;
        int nextInstIndex = -1;
        for (size_t i = 0; i < cpu.instructionTraces.size(); i++) {
            if (cpu.instructionTraces[i].address == nextPC) {
                nextInstIndex = i;
                break;
            }
        }
        
        if (nextInstIndex < 0 && nextPC / 4 < cpu.instMem.memory.size()) {
            uint32_t nextInstruction = cpu.instMem.readInstruction(nextPC);
            cpu.initInstructionTrace(nextPC, nextInstruction);
            nextInstIndex = cpu.instructionTraces.size() - 1;
        }
        
        if (nextInstIndex >= 0) cpu.trackInstructionStage(nextInstIndex, cpu.clockCycle - 1, "IF");
        
        cpu.idEx.valid = false;
        return;
    }
    
    bool isBranchOrJump = (cpu.ifId.instruction.format == B_TYPE || 
                           cpu.ifId.instruction.format == J_TYPE ||
                           (cpu.ifId.instruction.format == I_TYPE && cpu.ifId.instruction.opcode == JALR));
    
    if (isBranchOrJump) {
        // Initialize values
        int32_t rs1Value = 0;
        int32_t rs2Value = 0;
        
        if (isForwarding) {
            // First, check if branch depends on result still in ID/EX stage
            bool needsStall = false;
            if (cpu.ifId.instruction.rs1 != 0 && 
                cpu.idEx.valid && cpu.idEx.control.regWrite && 
                cpu.idEx.instruction.rd != 0 && 
                cpu.idEx.instruction.rd == cpu.ifId.instruction.rs1) {
                needsStall = true;
            }
            
            if (cpu.ifId.instruction.rs2 != 0 && 
                cpu.idEx.valid && cpu.idEx.control.regWrite && 
                cpu.idEx.instruction.rd != 0 && 
                cpu.idEx.instruction.rd == cpu.ifId.instruction.rs2) {
                needsStall = true;
            }
            
            if (needsStall) {
                // Need to stall because branch depends on previous instruction still in ID/EX
                stall = true;
                cpu.idEx.valid = false;
                return;
            }
            
            if (cpu.ifId.instruction.rs1 != 0) {
                if (cpu.exMem.valid && cpu.exMem.control.regWrite && 
                    cpu.exMem.instruction.rd != 0 && 
                    cpu.exMem.instruction.rd == cpu.ifId.instruction.rs1) {
                    rs1Value = cpu.exMem.aluResult.result;
                }
                else if (cpu.memWb.valid && cpu.memWb.control.regWrite && 
                         cpu.memWb.instruction.rd != 0 && 
                         cpu.memWb.instruction.rd == cpu.ifId.instruction.rs1) {
                    rs1Value = cpu.memWb.control.memToReg ? cpu.memWb.readData : cpu.memWb.aluResult;
                }
                else {
                    // No forwarding needed, read from register file
                    rs1Value = cpu.regFile.read(cpu.ifId.instruction.rs1);
                }
            }
            
            // Check for rs2 forwarding
            if (cpu.ifId.instruction.rs2 != 0) {
                if (cpu.exMem.valid && cpu.exMem.control.regWrite && 
                    cpu.exMem.instruction.rd != 0 && 
                    cpu.exMem.instruction.rd == cpu.ifId.instruction.rs2) {
                    rs2Value = cpu.exMem.aluResult.result;
                }
                else if (cpu.memWb.valid && cpu.memWb.control.regWrite && 
                         cpu.memWb.instruction.rd != 0 && 
                         cpu.memWb.instruction.rd == cpu.ifId.instruction.rs2) {
                    rs2Value = cpu.memWb.control.memToReg ? cpu.memWb.readData : cpu.memWb.aluResult;
                }
                else {
                    // No forwarding needed, read from register file
                    rs2Value = cpu.regFile.read(cpu.ifId.instruction.rs2);
                }
            }
        } else {
            // Without forwarding, just read from register file
            rs1Value = cpu.regFile.read(cpu.ifId.instruction.rs1);
            rs2Value = cpu.regFile.read(cpu.ifId.instruction.rs2);
        }
        
        // Rest of the branch logic using the potentially forwarded values
        // For JAL (J-type), always taken
        if (cpu.ifId.instruction.format == J_TYPE) {
            branchTaken = true;
            branchTarget = cpu.ifId.pc + cpu.ifId.instruction.immediate;
        }
        // For JALR, always taken with calculated target
        else if (cpu.ifId.instruction.opcode == JALR) {
            branchTaken = true;
            branchTarget = (rs1Value + cpu.ifId.instruction.immediate) & ~1; // Clear LSB
        }
        // For conditional branches (B-type), evaluate condition
        else if (cpu.ifId.instruction.format == B_TYPE) {
            bool conditionMet = false;

            switch (cpu.ifId.instruction.opcode) {
                case BEQ: conditionMet = (rs1Value == rs2Value); break;
                case BNE: conditionMet = (rs1Value != rs2Value); break;
                case BLT: conditionMet = (rs1Value < rs2Value); break;
                case BGE: conditionMet = (rs1Value >= rs2Value); break;
                case BLTU: conditionMet = (static_cast<uint32_t>(rs1Value) < static_cast<uint32_t>(rs2Value)); break;
                case BGEU: conditionMet = (static_cast<uint32_t>(rs1Value) >= static_cast<uint32_t>(rs2Value)); break;
                default: conditionMet = false;
            }

            // --------------------------------------------------------------------------------------
            // taking default condition not met
            // --------------------------------------------------------------------------------------
            // conditionMet = false;
            
            if (conditionMet) {
                branchTaken = true;
                branchTarget = cpu.ifId.pc + cpu.ifId.instruction.immediate;
            }
        }
    }
    
    cpu.idEx.pc = cpu.ifId.pc;
    cpu.idEx.instruction = cpu.ifId.instruction;
    cpu.idEx.readData1 = cpu.regFile.read(cpu.ifId.instruction.rs1);
    cpu.idEx.readData2 = cpu.regFile.read(cpu.ifId.instruction.rs2);
    cpu.idEx.immediate = cpu.ifId.instruction.immediate;
    
    cpu.setControlSignals(cpu.ifId.instruction, cpu.idEx.control);
    
    if (branchTaken) {
        cpu.idEx.control.branch = false;
        cpu.idEx.control.jump = false;
    }
    
    cpu.idEx.valid = true;
}

void executeStage(Processor& cpu, bool isForwarding = false) {
    if (!cpu.idEx.valid) {
        cpu.exMem.valid = false;
        return;
    }
    
    int instIndex = -1;
    for (size_t i = 0; i < cpu.instructionTraces.size(); i++) {
        if (cpu.instructionTraces[i].address == cpu.idEx.pc) {
            instIndex = i;
            break;
        }
    }
    
    if (instIndex >= 0) cpu.trackInstructionStage(instIndex, cpu.clockCycle - 1, "EX");
    
    cpu.exMem.pc = cpu.idEx.pc;
    cpu.exMem.control = cpu.idEx.control;
    cpu.exMem.readData2 = cpu.idEx.readData2;
    
    int32_t aluInput1, aluInput2;
    
    if (isForwarding) {
        cpu.forwardUnit.detectForwarding(cpu.idEx, cpu.exMem, cpu.memWb);
        
        switch (cpu.forwardUnit.forwardA) {
            case ForwardingUnit::FROM_EX_MEM:
                aluInput1 = cpu.exMem.aluResult.result;
                break;
            case ForwardingUnit::FROM_MEM_WB:
                aluInput1 = cpu.memWb.control.memToReg ? cpu.memWb.readData : cpu.memWb.aluResult;
                break;
            default:
                aluInput1 = cpu.idEx.readData1;
                break;
        }
        
        if (cpu.idEx.control.aluSrc) {
            aluInput2 = cpu.idEx.immediate;
        } else {
            switch (cpu.forwardUnit.forwardB) {
                case ForwardingUnit::FROM_EX_MEM:
                    aluInput2 = cpu.exMem.aluResult.result;
                    break;
                case ForwardingUnit::FROM_MEM_WB:
                    aluInput2 = cpu.memWb.control.memToReg ? cpu.memWb.readData : cpu.memWb.aluResult;
                    break;
                default:
                    aluInput2 = cpu.idEx.readData2;
                    break;
            }
        }
        
        if (cpu.idEx.control.memWrite) {
            switch (cpu.forwardUnit.forwardB) {
                case ForwardingUnit::FROM_EX_MEM:
                    cpu.exMem.readData2 = cpu.exMem.aluResult.result;
                    break;
                case ForwardingUnit::FROM_MEM_WB:
                    cpu.exMem.readData2 = cpu.memWb.control.memToReg ? cpu.memWb.readData : cpu.memWb.aluResult;
                    break;
                default:
                    break;
            }
        }
    } else {
        aluInput1 = cpu.idEx.readData1;
        aluInput2 = cpu.idEx.control.aluSrc ? cpu.idEx.immediate : cpu.idEx.readData2;
    }

    cpu.exMem.instruction = cpu.idEx.instruction;
    
    switch (cpu.idEx.instruction.opcode) {
        case ADD: case ADDI: case LB: case LH: case LW: case LBU: case LHU: case SB: case SH: case SW:
        case JALR:
            cpu.exMem.aluResult.result = aluInput1 + aluInput2;
            break;
        case SUB:
            cpu.exMem.aluResult.result = aluInput1 - aluInput2;
            break;
        case AND: case ANDI:
            cpu.exMem.aluResult.result = aluInput1 & aluInput2;
            break;
        case OR: case ORI:
            cpu.exMem.aluResult.result = aluInput1 | aluInput2;
            break;
        case XOR: case XORI:
            cpu.exMem.aluResult.result = aluInput1 ^ aluInput2;
            break;
        case SLL: case SLLI:
            cpu.exMem.aluResult.result = aluInput1 << (aluInput2 & 0x1F);
            break;
        case SRL: case SRLI:
            cpu.exMem.aluResult.result = static_cast<uint32_t>(aluInput1) >> (aluInput2 & 0x1F);
            break;
        case SRA: case SRAI:
            cpu.exMem.aluResult.result = aluInput1 >> (aluInput2 & 0x1F);
            break;
        case SLT: case SLTI: case BLT: case BGE:
            cpu.exMem.aluResult.result = (aluInput1 < aluInput2) ? 1 : 0;
            break;
        case SLTU: case SLTIU: case BLTU: case BGEU:
            cpu.exMem.aluResult.result = (static_cast<uint32_t>(aluInput1) < static_cast<uint32_t>(aluInput2)) ? 1 : 0;
            break;
        case BEQ:
            cpu.exMem.aluResult.result = (aluInput1 == aluInput2) ? 1 : 0;
            break;
        case BNE:
            cpu.exMem.aluResult.result = (aluInput1 != aluInput2) ? 1 : 0;
            break;
        case JAL:
            cpu.exMem.aluResult.result = cpu.idEx.pc + 4;  // Return address is PC + 4
            break;
        case LUI:
            cpu.exMem.aluResult.result = cpu.idEx.immediate;  // Load upper immediate
            break;
        case AUIPC:
            cpu.exMem.aluResult.result = cpu.idEx.pc + cpu.idEx.immediate;  // Add PC and upper immediate
            break;
        default:
            cpu.exMem.aluResult.result = 0;
            break;
    }
    
    cpu.exMem.aluResult.zero = (cpu.exMem.aluResult.result == 0);
    cpu.exMem.aluResult.negative = (cpu.exMem.aluResult.result < 0);
    
    cpu.exMem.valid = true;
}

void memoryStage(Processor& cpu) {
    if (!cpu.exMem.valid) {
        cpu.memWb.valid = false;
        return;
    }
    
    int instIndex = -1;
    for (size_t i = 0; i < cpu.instructionTraces.size(); i++) {
        if (cpu.instructionTraces[i].address == cpu.exMem.pc) {
            instIndex = i;
            break;
        }
    }
    
    if (instIndex >= 0) cpu.trackInstructionStage(instIndex, cpu.clockCycle - 1, "MEM");
    
    cpu.memWb.instruction = cpu.exMem.instruction;
    cpu.memWb.pc = cpu.exMem.pc;
    cpu.memWb.control = cpu.exMem.control;
    cpu.memWb.aluResult = cpu.exMem.aluResult.result;
    
    if (cpu.exMem.control.memRead) {
        uint32_t address = cpu.exMem.aluResult.result;
        
        switch (cpu.exMem.instruction.opcode) {
            case LB:
                cpu.memWb.readData = cpu.dataMem.read(address, 1);
                if (cpu.memWb.readData & 0x80) cpu.memWb.readData |= 0xFFFFFF00;
                break;
            case LH:
                cpu.memWb.readData = cpu.dataMem.read(address, 2);
                if (cpu.memWb.readData & 0x8000) cpu.memWb.readData |= 0xFFFF0000;
                break;
            case LW:
                cpu.memWb.readData = cpu.dataMem.read(address, 4);
                break;
            case LBU:
                cpu.memWb.readData = cpu.dataMem.read(address, 1) & 0xFF;
                break;
            case LHU:
                cpu.memWb.readData = cpu.dataMem.read(address, 2) & 0xFFFF;
                break;
            default:
                cpu.memWb.readData = 0;
                break;
        }
    } else {
        cpu.memWb.readData = 0;
    }
    
    if (cpu.exMem.control.memWrite) {
        uint32_t address = cpu.exMem.aluResult.result;
        int32_t value = cpu.exMem.readData2;
        
        switch (cpu.exMem.instruction.opcode) {
            case SB: cpu.dataMem.write(address, value, 1); break;
            case SH: cpu.dataMem.write(address, value, 2); break;
            case SW: cpu.dataMem.write(address, value, 4); break;
            default: break;
        }
    }
    
    cpu.memWb.valid = true;
}

void writeBackStage(Processor& cpu) {
    if (!cpu.memWb.valid) return;
    
    int instIndex = -1;
    for (size_t i = 0; i < cpu.instructionTraces.size(); i++) {
        if (cpu.instructionTraces[i].address == cpu.memWb.pc) {
            instIndex = i;
            break;
        }
    }
    
    if (instIndex >= 0) cpu.trackInstructionStage(instIndex, cpu.clockCycle - 1, "WB");
    
    if (cpu.memWb.control.regWrite && cpu.memWb.instruction.rd != 0) {
        int32_t writeData = cpu.memWb.control.memToReg ? cpu.memWb.readData : cpu.memWb.aluResult;
        cpu.regFile.write(cpu.memWb.instruction.rd, writeData);
    }
    
    cpu.instructionsExecuted++;
}

void executePipeline(Processor& cpu, int cycles, bool isForwarding = false) {
    cpu.instructionTraces.clear();
    cpu.reset();
    
    for (size_t i = 0; i < cpu.instMem.memory.size(); i++) {
        uint32_t pc = i * 4;
        uint32_t instruction = cpu.instMem.readInstruction(pc);
        
        bool alreadyTracked = false;
        for (const auto& trace : cpu.instructionTraces) {
            if (trace.address == pc) {
                alreadyTracked = true;
                break;
            }
        }
        
        if (!alreadyTracked) cpu.initInstructionTrace(pc, instruction);
    }
    
    cpu.pc = 0;
    
    std::cout << "Running pipeline with " << (isForwarding ? "forwarding enabled" : "forwarding disabled") << std::endl;
    
    for (int i = 0; i < cycles; i++) {
        cpu.clockCycle++;
        
        writeBackStage(cpu);
        memoryStage(cpu);
        executeStage(cpu, isForwarding);
        
        bool stall = false;
        bool branchTaken = false;
        uint32_t branchTarget = 0;
        
        instructionDecodeStage(cpu, stall, branchTaken, branchTarget, isForwarding);
        instructionFetchStage(cpu, stall, isForwarding);
        
        if (branchTaken) {
            cpu.pc = branchTarget;
            cpu.ifId.valid = false;
        }
        
        if (cpu.exMem.valid && ((cpu.exMem.control.branch && cpu.exMem.branchTaken) || 
                              cpu.exMem.control.jump)) {
            cpu.pc = cpu.exMem.branchTarget;
            cpu.ifId.valid = false;
        }
    }
    
    cpu.outputPipelineTraceCSV();
    cpu.outputPipelineTraceTXT();
    cpu.printTerminalTrace();
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <filename> <cyclecount>" << std::endl;
        return 1;
    }

    std::string file = argv[1];
    const int cyclecount = std::stoi(argv[2]);
    
    std::ifstream inputFile(file);
    if (!inputFile) {
        std::cerr << "Error: Could not open file " << file << std::endl;
        return 1;
    }

    std::string line;
    Processor cpu;

    while (getline(inputFile, line)) {
        std::istringstream iss(line);
        uint32_t machinecode; std::string assemblycode;
        std::getline(iss >> std::hex >> machinecode >> std::ws, assemblycode);
        cpu.instMem.memory.push_back(machinecode);
    }

    inputFile.close();
    
    bool is_forwarding = true;
    
    std::string filename = is_forwarding ? "pipeline_trace_forwarding.csv" : "pipeline_trace_no_forwarding.csv";
    cpu.openTraceFile(filename);
    if (is_forwarding)
        cpu.openOutputFile(file+"_forward_out.txt");
    else
        cpu.openOutputFile(file+"_noforward_out.txt");
    executePipeline(cpu, cyclecount, is_forwarding);
    cpu.closeTraceFile();
    cpu.closeOutputFile();

    if (is_forwarding)
        remove("pipeline_trace_forwarding.csv");
    else
        remove("pipeline_trace_no_forwarding.csv");
        
    return 0;
}
