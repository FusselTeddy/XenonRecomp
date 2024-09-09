#include <file.h>
#include <image.h>
#include <function.h>
#include <format>
#include <print>
#include <disasm.h>
#include <filesystem>

#define TEST_FILE "add.elf"

int main()
{
    const auto file = LoadFile(TEST_FILE).value();
    auto image = Image::ParseImage(file.data(), file.size()).value();

    // TODO: Load functions from an existing database
    std::vector<Function> functions;
    for (const auto& section : image.sections)
    {
        if (!(section.flags & SectionFlags_Code))
        {
            continue;
        }

        size_t base = section.base;
        uint8_t* data = section.data;
        uint8_t* dataEnd = section.data + section.size;
        while (data < dataEnd)
        {
            if (*(uint32_t*)data == 0)
            {
                data += 4;
                base += 4;
                continue;
            }

            const auto& fn = functions.emplace_back(Function::Analyze(data, dataEnd - data, base));
            data += fn.size;
            base += fn.size;

            image.symbols.emplace(std::format("sub_{:X}", fn.base), fn.base, fn.size, Symbol_Function);
        }
    }

    std::filesystem::create_directory("out");
    FILE* f = fopen("out/" TEST_FILE ".cpp", "w");
    for (const auto& fn : functions)
    {
        auto base = fn.base;
        auto end = base + fn.size;
        auto* data = (uint32_t*)image.Find(base);

        std::string name = "";
        auto symbol = image.symbols.find(base);
        if (symbol != image.symbols.end())
        {
            name = symbol->name;
        }
        else
        {
            name = std::format("sub_{:X}", base);
        }

        std::println(f, "void {}() {{", name);

        ppc_insn insn;
        while (base < end)
        {
            ppc::Disassemble(data, 4, base, insn);

            base += 4;
            ++data;
            if (insn.opcode == nullptr)
            {
                std::println(f, "\t// {:x} {}", base - 4, insn.op_str);
            }
            else
            {
                std::println(f, "\t// {:x} {} {}", base - 4, insn.opcode->name, insn.op_str);
                switch (insn.opcode->id)
                {
                case PPC_INST_ADD:
                    std::println(f, "\tr{} = r{} + r{};", insn.operands[0], insn.operands[1], insn.operands[2]);
                    break;
                case PPC_INST_ADDI:
                    std::println(f, "\tr{} = r{} + {};", insn.operands[0], insn.operands[1], insn.operands[2]);
                    break;
                case PPC_INST_STWU:
                    std::println(f, "\tea = r{} + {};", insn.operands[2], static_cast<int32_t>(insn.operands[1]));
                    std::println(f, "\t*ea = byteswap(r{});", insn.operands[0]);
                    std::println(f, "\tr{} = ea;", insn.operands[2]);
                    break;
                case PPC_INST_STW:
                    std::println(f, "\t*(r{} + {}) = byteswap(r{});", insn.operands[2], static_cast<int32_t>(insn.operands[1]), insn.operands[0]);
                    break;
                case PPC_INST_MR:
                    std::println(f, "\tr{} = r{};", insn.operands[0], insn.operands[1]);
                    break;
                case PPC_INST_LWZ:
                    std::println(f, "\tr{} = *(r{} + {});", insn.operands[0], insn.operands[2], insn.operands[1]);
                    break;
                case PPC_INST_LI:
                    std::println(f, "\tr{} = {};", insn.operands[0], insn.operands[1]);
                    break;
                case PPC_INST_MFLR:
                    std::println(f, "\tr{} = lr;", insn.operands[0]);
                    break;
                case PPC_INST_MTLR:
                    std::println(f, "\tlr = r{};", insn.operands[0]);
                    break;
                case PPC_INST_BLR:
                    std::println(f, "\treturn;");
                    break;
                case PPC_INST_BL:
                {
                    std::string targetName = "";
                    auto targetSymbol = image.symbols.find(insn.operands[0]);
                    if (targetSymbol != image.symbols.end() && targetSymbol->type == Symbol_Function)
                    {
                        targetName = targetSymbol->name;
                    }
                    else
                    {
                        targetName = std::format("sub_{:X}", insn.operands[0]);
                    }
                    std::println(f, "\tlr = 0x{:x};", base);
                    std::println(f, "\t{}();", targetName);
                    break;
                }
                }
            }
        }

        std::println(f, "}}\n");
    }

    fclose(f);

    return 0;
}
