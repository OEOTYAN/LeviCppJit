#include <llvm/ExecutionEngine/JITLink/JITLink.h>
#include <llvm/ExecutionEngine/JITLink/x86_64.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Object/COFF.h>

namespace lcj {
class COFFHeaderMaterializationUnit : public llvm::orc::MaterializationUnit {
public:
    COFFHeaderMaterializationUnit(
        llvm::orc::LLJIT&                 Jit,
        llvm::orc::SymbolStringPtr const& HeaderStartSymbol
    )
    : MaterializationUnit(createHeaderInterface(HeaderStartSymbol)),
      Jit(Jit) {}

    [[nodiscard]] llvm::StringRef getName() const override { return "COFFHeaderMU"; }

    void materialize(std::unique_ptr<llvm::orc::MaterializationResponsibility> R) override {

        auto G = std::make_unique<llvm::jitlink::LinkGraph>(
            "<COFFHeaderMU>",
            Jit.getTargetTriple(),
            8,
            llvm::endianness::little,
            llvm::jitlink::getGenericEdgeKindName
        );
        auto& HeaderSection = G->createSection("__header", llvm::orc::MemProt::Read);
        auto& HeaderBlock   = createHeaderBlock(*G, HeaderSection);

        // Init symbol is __ImageBase symbol.
        auto& ImageBaseSymbol = G->addDefinedSymbol(
            HeaderBlock,
            0,
            *R->getInitializerSymbol(),
            HeaderBlock.getSize(),
            llvm::jitlink::Linkage::Strong,
            llvm::jitlink::Scope::Default,
            false,
            true
        );

        addImageBaseRelocationEdge(HeaderBlock, ImageBaseSymbol);

        cast<llvm::orc::ObjectLinkingLayer>(Jit.getObjLinkingLayer())
            .emit(std::move(R), std::move(G));
    }

    void discard(const llvm::orc::JITDylib& JD, const llvm::orc::SymbolStringPtr& Sym) override {}

private:
    llvm::orc::LLJIT& Jit;

    struct HeaderSymbol {
        const char* Name;
        uint64_t    Offset;
    };

    struct NTHeader {
        llvm::support::ulittle32_t     PEMagic;
        llvm::object::coff_file_header FileHeader;
        struct PEHeader {
            llvm::object::pe32plus_header Header;
            llvm::object::data_directory  DataDirectory[llvm::COFF::NUM_DATA_DIRECTORIES + 1];
        } OptionalHeader;
    };

    struct HeaderBlockContent {
        llvm::object::dos_header DOSHeader;
        NTHeader                 NTHeader;
    };

    static llvm::jitlink::Block&
    createHeaderBlock(llvm::jitlink::LinkGraph& G, llvm::jitlink::Section& HeaderSection) {
        HeaderBlockContent Hdr = {};

        // Set up magic
        Hdr.DOSHeader.Magic[0]              = 'M';
        Hdr.DOSHeader.Magic[1]              = 'Z';
        Hdr.DOSHeader.AddressOfNewExeHeader = offsetof(HeaderBlockContent, NTHeader);
        uint32_t PEMagic     = *reinterpret_cast<const uint32_t*>(llvm::COFF::PEMagic);
        Hdr.NTHeader.PEMagic = PEMagic;
        Hdr.NTHeader.OptionalHeader.Header.Magic = llvm::COFF::PE32Header::PE32_PLUS;

        Hdr.NTHeader.FileHeader.Machine = llvm::COFF::IMAGE_FILE_MACHINE_AMD64;

        auto HeaderContent =
            G.allocateContent(llvm::ArrayRef<char>(reinterpret_cast<const char*>(&Hdr), sizeof(Hdr))
            );

        return G.createContentBlock(HeaderSection, HeaderContent, llvm::orc::ExecutorAddr(), 8, 0);
    }

    static void
    addImageBaseRelocationEdge(llvm::jitlink::Block& B, llvm::jitlink::Symbol& ImageBase) {
        auto ImageBaseOffset = offsetof(HeaderBlockContent, NTHeader)
                             + offsetof(NTHeader, OptionalHeader)
                             + offsetof(llvm::object::pe32plus_header, ImageBase);
        B.addEdge(llvm::jitlink::x86_64::Pointer64, ImageBaseOffset, ImageBase, 0);
    }

    static MaterializationUnit::Interface
    createHeaderInterface(const llvm::orc::SymbolStringPtr& HeaderStartSymbol) {
        llvm::orc::SymbolFlagsMap HeaderSymbolFlags;

        HeaderSymbolFlags[HeaderStartSymbol] = llvm::JITSymbolFlags::Exported;

        return {std::move(HeaderSymbolFlags), HeaderStartSymbol};
    }
};
} // namespace lcj