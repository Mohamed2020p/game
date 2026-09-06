// =============================================================================
//  il2cpp_demo.h — an IL2CPP metadata format lesson in one file
// =============================================================================
//
//  How real IL2CPP games store their C# type information
//  -----------------------------------------------------
//  A Unity IL2CPP build ships two interesting artifacts:
//
//    * libil2cpp.so    — the AOT-compiled machine code, plus runtime data
//                        structures (Il2CppClass, MethodInfo, ...) whose
//                        layouts are defined by Unity's headers.
//    * global-metadata.dat — a binary catalog of *every* C# type, method and
//                        field name, stored (in an APK) at
//                        assets/bin/Data/Managed/Metadata/global-metadata.dat.
//
//  The metadata file starts with a header whose first two fields are a magic
//  (0xFAB11BAF) and a format version. Everything else in the file is sections
//  addressed by (offset, size) pairs stored IN the header: a string table,
//  type definitions, field definitions, method definitions, and so on.
//  Parsers (public research tools like Il2CppDumper / Il2CppInspector — which
//  are the right way to study a build you own) walk exactly these structures.
//
//  What THIS file does
//  -------------------
//  1. BUILDS a small metadata blob at startup, byte-layout faithful to the
//     classic v24.x section formats (string table, Il2CppTypeDefinition,
//     Il2CppFieldDefinition) but describing the demo game classes in
//     demo_game.h. Nothing here touches any external file.
//
//  2. PARSES that blob the way a real parser would: validate magic+version,
//     bounds-check every section access, resolve names through the string
//     table, and recover per-field offsets.
//
//  3. VALIDATES the recovered offsets against the actual compiled layout of
//     GameRoot via offsetof() — exactly the "does my dump match this binary?"
//     check you'd do with a real game, performed at startup so the overlay's
//     status card can show it.
//
//  One honest simplification, called out loudly:
//  real global-metadata does NOT usually contain instance field *offsets* —
//  the runtime computes them and stores them in Il2CppClass at load time, and
//  dumping tools recover them from the compiled binary. To keep this demo
//  self-contained we append a clearly-marked "demo extension" section that
//  plays the role of that recovered offset data. Every other byte you see
//  mirrors the real v24.x format.
// =============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "demo_game.h"

namespace il2cpp {

// -----------------------------------------------------------------------------
// Real-format constants
// -----------------------------------------------------------------------------

constexpr uint32_t kMetadataSanity  = 0xFAB11BAFu;  // real IL2CPP magic
constexpr int32_t  kMetadataVersion = 24;           // v24.x section layouts

// Abridged v24.x global metadata header. Field ORDER and SIZES match the real
// Il2CppGlobalMetadataHeader for the sections implemented here; the real
// header continues with images/assembly/method-refs/metadata-usage sections
// that this demo does not need (marked ---).
struct MetadataHeader {
    int32_t sanity;                                  // 0xFAB11BAF
    int32_t version;                                 // 24

    int32_t stringLiteralOffset, stringLiteralSize;
    int32_t stringLiteralDataOffset, stringLiteralDataSize;
    int32_t stringOffset, stringSize;                // names (NUL-separated)
    int32_t eventsOffset, eventsSize;                // --- empty
    int32_t propertiesOffset, propertiesSize;        // --- empty
    int32_t methodsOffset, methodsSize;              // --- empty
    int32_t parameterDefaultValuesOffset, parameterDefaultValuesSize;
    int32_t fieldDefaultValuesOffset, fieldDefaultValuesSize;
    int32_t fieldAndParameterDefaultValueDataOffset, fieldAndParameterDefaultValueDataSize;
    int32_t fieldMarshaledSizesOffset, fieldMarshaledSizesSize;
    int32_t parametersOffset, parametersSize;        // --- empty
    int32_t fieldsOffset, fieldsSize;                // Il2CppFieldDefinition[]
    int32_t genericParametersOffset, genericParametersSize;
    int32_t genericParameterConstraintsOffset, genericParameterConstraintsSize;
    int32_t genericContainersOffset, genericContainersSize;
    int32_t nestedTypesOffset, nestedTypesSize;
    int32_t interfacesOffset, interfacesSize;
    int32_t vtableMethodsOffset, vtableMethodsSize;
    int32_t interfaceOffsetsOffset, interfaceOffsetsSize;
    int32_t typeDefinitionsOffset, typeDefinitionsSize;  // Il2CppTypeDefinition[]

    // Demo extension (NOT real format): recovered field-offset data.
    int32_t demoFieldOffsetsOffset, demoFieldOffsetsSize;
};

// Il2CppTypeDefinition, v24.x layout (92 bytes — asserted below). Field names
// and order mirror Unity's Il2CppMetadataRegistration headers.
struct TypeDefinitionV24 {
    int32_t nameIndex;
    int32_t namespaceIndex;
    int32_t byvalTypeIndex;
    int32_t byrefTypeIndex;
    int32_t declaringTypeIndex;
    int32_t parentIndex;
    int32_t elementTypeIndex;
    int32_t genericContainerIndex;
    uint32_t flags;
    int32_t fieldStart;
    int32_t methodStart;
    int32_t eventStart;
    int32_t propertyStart;
    int32_t nestedTypesStart;
    int32_t interfacesStart;
    int32_t vtableStart;
    int32_t interfaceOffsetsStart;
    uint16_t method_count;
    uint16_t property_count;
    uint16_t field_count;
    uint16_t event_count;
    uint16_t nested_type_count;
    uint16_t vtable_count;
    uint16_t interfaces_count;
    uint16_t interface_offsets_count;
    uint32_t bitfield;
    uint32_t token;
};

// Il2CppFieldDefinition — 12 bytes.
struct FieldDefinitionV24 {
    int32_t  nameIndex;
    int32_t  typeIndex;
    uint32_t token;
};

static_assert(sizeof(TypeDefinitionV24) == 92, "v24.x TypeDefinition must be 92 bytes");
static_assert(sizeof(FieldDefinitionV24) == 12, "FieldDefinition must be 12 bytes");
static_assert(offsetof(MetadataHeader, stringOffset) == 24,
              "header field order drifted from the real v24.x layout");

// -----------------------------------------------------------------------------
// Blob builder — synthesizes global-metadata-style bytes for the demo game
// -----------------------------------------------------------------------------

// Per-class layout data we "recover" (demo-extension section payload).
struct FieldLayoutEntry {
    int32_t nameIndex;
    int32_t offset;   // byte offset of the field inside its struct
    int32_t size;     // sizeof the field (arrays multiply by element count)
};

struct ClassLayout {
    int32_t typeDefIndex;
    std::vector<FieldLayoutEntry> fields;
};

// Builds the whole blob. Returned bytes are exactly what Parse() consumes —
// feed them through the parser yourself to see the round-trip.
std::vector<uint8_t> BuildDemoMetadataBlob();

// The demo class catalog: (class name, namespace, field names) and the layout
// entries derived from the ACTUAL compiled structs via offsetof/sizeof, so the
// metadata can never silently disagree with the code that uses it.
struct ClassSpec {
    const char* name;
    const char* namespaze;   // 'namespace' is a keyword
    std::vector<const char*> fieldNames;
};

inline std::vector<ClassSpec> DemoClassCatalog() {
    return {
        {"GameRoot", "DemoGame",
         {"header", "player", "vehicles", "ores", "workers", "economy",
          "settings", "footer", "checksum"}},
        {"Player", "DemoGame",
         {"health", "maxHealth", "stamina", "posX", "posY", "baseMoveSpeed",
          "level", "xp"}},
        {"Vehicle", "DemoGame",
         {"name", "fuel", "fuelMax", "speedMul", "engineOn", "refuelCount"}},
        {"OreVein", "DemoGame",
         {"name", "amount", "richness", "tier", "price"}},
        {"Worker", "DemoGame",
         {"name", "state", "efficiency", "fatigue", "oreIndex"}},
        {"Economy", "DemoGame",
         {"coins", "gems", "incomePerSec", "pendingIncome"}},
        {"GameSettings", "DemoGame",
         {"timeScale", "moveSpeedMul", "vehicleSpeedMul", "autoSmelt",
          "staminaBoost", "fastHaul"}},
    };
}

// Recovered layout for one class — computed from the live structs via
// offsetof()/sizeof() in BuildDemoMetadataBlob(). There is no separate stub
// table here on purpose: the metadata can never disagree with the code.

// -----------------------------------------------------------------------------
// Parser — walks the blob like a real metadata parser would
// -----------------------------------------------------------------------------

struct FieldInfo {
    std::string name;
    int32_t     offset = 0;   // from the demo-extension section
    int32_t     size   = 0;
    uint32_t    token  = 0;
    int32_t     typeIndex = 0;  // raw from Il2CppFieldDefinition
};

struct ClassInfo {
    std::string name;
    std::string namespaze;
    uint32_t    token = 0;
    uint32_t    flags = 0;
    std::vector<FieldInfo> fields;
};

class Metadata {
public:
    // Parse a blob. Fails (false) on bad magic/version or truncated sections —
    // a real parser must treat the file as hostile input, and this one does.
    bool Parse(const uint8_t* data, size_t size);

    const std::vector<ClassInfo>& classes() const { return classes_; }
    const ClassInfo* FindClass(const std::string& name) const;
    const FieldInfo* FindField(const std::string& className,
                               const std::string& fieldName) const;

    bool ok() const { return ok_; }
    int32_t version() const { return version_; }
    size_t stringTableSize() const { return stringTable_.size(); }

    // Human-readable dump of everything parsed (drives the Misc tab viewer).
    std::string Describe() const;

    // Cross-checks parsed class layouts against the compiled GameRoot layout
    // using offsetof()/sizeof(). Returns an empty string on success, otherwise
    // a list of mismatches. This is the "my dump matches this binary" test.
    std::string ValidateAgainstGameLayout() const;

private:
    template <typename T>
    const T* SectionAt(int32_t offset, int32_t sizeBytes, size_t blobSize) const {
        if (offset < 0 || sizeBytes < 0) return nullptr;
        if (static_cast<size_t>(offset) + static_cast<size_t>(sizeBytes) > blobSize)
            return nullptr;
        if (static_cast<size_t>(sizeBytes) < sizeof(T)) return nullptr;
        return reinterpret_cast<const T*>(data_.data() + offset);           // NOLINT
    }

    const char* StringAt(int32_t index) const {
        if (index < 0 || static_cast<size_t>(index) >= stringTable_.size())
            return "<bad-index>";
        return stringTable_.data() + index;                                 // NOLINT
    }

    std::vector<uint8_t> data_;     // parser owns its copy — never parse in place
    std::string          stringTable_;
    std::vector<ClassInfo> classes_;
    int32_t version_ = 0;
    bool    ok_      = false;
};

// =============================================================================
//  Implementation (header-only so the host test can use it too)
// =============================================================================

inline std::vector<uint8_t> BuildDemoMetadataBlob() {
    const auto catalog = DemoClassCatalog();

    // ---- pass 0: recover field layouts from the compiled structs ------------
    // Offset/size come straight from offsetof()/sizeof() — the data a real
    // project recovers from the dumped binary. Name indices are joined in
    // after the string table exists (see "join" below).
    struct RawField { int32_t offset; int32_t size; };
    const std::vector<std::vector<RawField>> rawLayouts = {
        // GameRoot
        {{(int32_t)offsetof(demo::GameRoot, header),   (int32_t)sizeof(demo::GameHeader)},
         {(int32_t)offsetof(demo::GameRoot, player),   (int32_t)sizeof(demo::Player)},
         {(int32_t)offsetof(demo::GameRoot, vehicles), (int32_t)sizeof(demo::Vehicle)},
         {(int32_t)offsetof(demo::GameRoot, ores),     (int32_t)sizeof(demo::OreVein)},
         {(int32_t)offsetof(demo::GameRoot, workers),  (int32_t)sizeof(demo::Worker)},
         {(int32_t)offsetof(demo::GameRoot, economy),  (int32_t)sizeof(demo::Economy)},
         {(int32_t)offsetof(demo::GameRoot, settings), (int32_t)sizeof(demo::GameSettings)},
         {(int32_t)offsetof(demo::GameRoot, footer),   4},
         {(int32_t)offsetof(demo::GameRoot, checksum), 4}},
        // Player
        {{(int32_t)offsetof(demo::Player, health), 4},
         {(int32_t)offsetof(demo::Player, maxHealth), 4},
         {(int32_t)offsetof(demo::Player, stamina), 4},
         {(int32_t)offsetof(demo::Player, posX), 4},
         {(int32_t)offsetof(demo::Player, posY), 4},
         {(int32_t)offsetof(demo::Player, baseMoveSpeed), 4},
         {(int32_t)offsetof(demo::Player, level), 4},
         {(int32_t)offsetof(demo::Player, xp), 4}},
        // Vehicle
        {{0, 16},
         {(int32_t)offsetof(demo::Vehicle, fuel), 4},
         {(int32_t)offsetof(demo::Vehicle, fuelMax), 4},
         {(int32_t)offsetof(demo::Vehicle, speedMul), 4},
         {(int32_t)offsetof(demo::Vehicle, engineOn), 4},
         {(int32_t)offsetof(demo::Vehicle, refuelCount), 4}},
        // OreVein
        {{0, 16},
         {(int32_t)offsetof(demo::OreVein, amount), 4},
         {(int32_t)offsetof(demo::OreVein, richness), 4},
         {(int32_t)offsetof(demo::OreVein, tier), 4},
         {(int32_t)offsetof(demo::OreVein, price), 4}},
        // Worker
        {{0, 16},
         {(int32_t)offsetof(demo::Worker, state), 4},
         {(int32_t)offsetof(demo::Worker, efficiency), 4},
         {(int32_t)offsetof(demo::Worker, fatigue), 4},
         {(int32_t)offsetof(demo::Worker, oreIndex), 4}},
        // Economy
        {{(int32_t)offsetof(demo::Economy, coins), 8},
         {(int32_t)offsetof(demo::Economy, gems), 4},
         {(int32_t)offsetof(demo::Economy, incomePerSec), 4},
         {(int32_t)offsetof(demo::Economy, pendingIncome), 8}},
        // GameSettings
        {{(int32_t)offsetof(demo::GameSettings, timeScale), 4},
         {(int32_t)offsetof(demo::GameSettings, moveSpeedMul), 4},
         {(int32_t)offsetof(demo::GameSettings, vehicleSpeedMul), 4},
         {(int32_t)offsetof(demo::GameSettings, autoSmelt), 4},
         {(int32_t)offsetof(demo::GameSettings, staminaBoost), 4},
         {(int32_t)offsetof(demo::GameSettings, fastHaul), 4}},
    };

    // sanity: the catalog and the layout table must agree
    if (catalog.size() != rawLayouts.size()) {
        std::fprintf(stderr, "il2cpp_demo: catalog/layout class-count mismatch\n");
        return {};
    }
    for (size_t i = 0; i < catalog.size(); ++i)
        if (catalog[i].fieldNames.size() != rawLayouts[i].size()) {
            std::fprintf(stderr, "il2cpp_demo: catalog/layout mismatch for %s\n",
                         catalog[i].name);
            return {};
        }

    // ---- pass 1: string table ----------------------------------------------
    std::vector<uint8_t> strings;
    auto AddString = [&](const char* s) -> int32_t {
        const int32_t off = (int32_t)strings.size();
        strings.insert(strings.end(), s, s + std::strlen(s) + 1);  // keep NUL
        return off;
    };
    // NOTE: order matters — AddString must run while `strings` is final-sized
    // per push, and offsets are recorded BEFORE insertion. (Done above.)
    std::vector<int32_t> classNameIdx(catalog.size());
    std::vector<std::vector<int32_t>> fieldNameIdx(catalog.size());
    const int32_t nsIndex = AddString("DemoGame");
    for (size_t i = 0; i < catalog.size(); ++i) {
        classNameIdx[i] = AddString(catalog[i].name);
        fieldNameIdx[i].reserve(catalog[i].fieldNames.size());
        for (const char* fn : catalog[i].fieldNames)
            fieldNameIdx[i].push_back(AddString(fn));
    }

    // Join pass: names (pass 1) x offsets (pass 0) -> full layout entries.
    std::vector<std::vector<FieldLayoutEntry>> layouts(catalog.size());
    for (size_t i = 0; i < catalog.size(); ++i) {
        layouts[i].reserve(rawLayouts[i].size());
        for (size_t f = 0; f < rawLayouts[i].size(); ++f)
            layouts[i].push_back({fieldNameIdx[i][f],
                                  rawLayouts[i][f].offset,
                                  rawLayouts[i][f].size});
    }

    // ---- pass 2: field definitions (all classes share one array) ------------
    std::vector<FieldDefinitionV24> fieldDefs;
    std::vector<int32_t> fieldStartPerClass(catalog.size(), -1);
    uint32_t nextToken = 0x04000001u;   // field tokens are 0x0400xxxx in ECMA-335
    for (size_t i = 0; i < catalog.size(); ++i) {
        fieldStartPerClass[i] = (int32_t)fieldDefs.size();
        for (size_t f = 0; f < catalog[i].fieldNames.size(); ++f) {
            fieldDefs.push_back({fieldNameIdx[i][f], /*typeIndex*/ 0, nextToken++});
        }
    }

    // ---- pass 3: type definitions -------------------------------------------
    std::vector<TypeDefinitionV24> typeDefs;
    for (size_t i = 0; i < catalog.size(); ++i) {
        TypeDefinitionV24 td{};
        td.nameIndex        = classNameIdx[i];
        td.namespaceIndex   = nsIndex;
        td.byvalTypeIndex   = (int32_t)i;      // would index Il2CppType[] for real
        td.byrefTypeIndex   = -1;
        td.declaringTypeIndex = -1;
        td.parentIndex      = -1;             // no inheritance modeled
        td.elementTypeIndex = -1;
        td.genericContainerIndex = -1;
        td.flags            = 0x00100001u;     // "public, sealed, beforefieldinit"-ish
        td.fieldStart       = fieldStartPerClass[i];
        td.methodStart      = -1;
        td.eventStart       = -1;
        td.propertyStart    = -1;
        td.nestedTypesStart = -1;
        td.interfacesStart  = -1;
        td.vtableStart      = -1;
        td.interfaceOffsetsStart = -1;
        td.method_count     = 0;
        td.property_count   = 0;
        td.field_count      = (uint16_t)catalog[i].fieldNames.size();
        td.event_count      = 0;
        td.nested_type_count = 0;
        td.vtable_count     = 0;
        td.interfaces_count = 0;
        td.interface_offsets_count = 0;
        td.bitfield         = 0;
        td.token            = 0x02000001u + (uint32_t)i;  // typedef token
        typeDefs.push_back(td);
    }

    // ---- pass 4: demo-extension field offsets -------------------------------
    // Layout: int32 classCount, then per class:
    //   int32 typeDefIndex, int32 fieldCount, then fieldCount * FieldLayoutEntry
    std::vector<uint8_t> demoSection;
    auto AppendI32 = [&demoSection](int32_t v) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);    // NOLINT
        demoSection.insert(demoSection.end(), p, p + 4);
    };
    AppendI32((int32_t)catalog.size());
    for (size_t i = 0; i < catalog.size(); ++i) {
        AppendI32((int32_t)i);
        AppendI32((int32_t)layouts[i].size());
        for (const FieldLayoutEntry& e : layouts[i]) {
            AppendI32(e.nameIndex);
            AppendI32(e.offset);
            AppendI32(e.size);
        }
    }

    // ---- assemble the blob and fill in the (offset,size) header pairs -------
    // Sections are laid out in the header's declared order; every section the
    // demo doesn't use gets offset=payloadStart, size=0 — exactly how an empty
    // section looks in a real file.
    std::vector<uint8_t> blob;
    blob.resize(sizeof(MetadataHeader));      // header first, patched last
    auto Append = [&blob](const void* p, size_t n) {
        const int32_t off = (int32_t)blob.size();
        const auto* bytes = static_cast<const uint8_t*>(p);
        blob.insert(blob.end(), bytes, bytes + n);
        return off;
    };

    MetadataHeader h{};
    const int32_t emptyAt = (int32_t)blob.size();

    h.sanity  = (int32_t)kMetadataSanity;
    h.version = kMetadataVersion;

    h.stringLiteralOffset = emptyAt;  h.stringLiteralSize = 0;
    h.stringLiteralDataOffset = emptyAt; h.stringLiteralDataSize = 0;
    h.stringOffset = Append(strings.data(), strings.size());
    h.stringSize   = (int32_t)strings.size();
    h.eventsOffset = emptyAt;  h.eventsSize = 0;
    h.propertiesOffset = emptyAt;  h.propertiesSize = 0;
    h.methodsOffset = emptyAt;  h.methodsSize = 0;
    h.parameterDefaultValuesOffset = emptyAt;  h.parameterDefaultValuesSize = 0;
    h.fieldDefaultValuesOffset = emptyAt;  h.fieldDefaultValuesSize = 0;
    h.fieldAndParameterDefaultValueDataOffset = emptyAt;
    h.fieldAndParameterDefaultValueDataSize = 0;
    h.fieldMarshaledSizesOffset = emptyAt;  h.fieldMarshaledSizesSize = 0;
    h.parametersOffset = emptyAt;  h.parametersSize = 0;
    h.fieldsOffset = Append(fieldDefs.data(), fieldDefs.size() * sizeof(FieldDefinitionV24));
    h.fieldsSize   = (int32_t)(fieldDefs.size() * sizeof(FieldDefinitionV24));
    h.genericParametersOffset = emptyAt;  h.genericParametersSize = 0;
    h.genericParameterConstraintsOffset = emptyAt;  h.genericParameterConstraintsSize = 0;
    h.genericContainersOffset = emptyAt;  h.genericContainersSize = 0;
    h.nestedTypesOffset = emptyAt;  h.nestedTypesSize = 0;
    h.interfacesOffset = emptyAt;  h.interfacesSize = 0;
    h.vtableMethodsOffset = emptyAt;  h.vtableMethodsSize = 0;
    h.interfaceOffsetsOffset = emptyAt;  h.interfaceOffsetsSize = 0;
    h.typeDefinitionsOffset = Append(typeDefs.data(), typeDefs.size() * sizeof(TypeDefinitionV24));
    h.typeDefinitionsSize   = (int32_t)(typeDefs.size() * sizeof(TypeDefinitionV24));
    h.demoFieldOffsetsOffset = Append(demoSection.data(), demoSection.size());
    h.demoFieldOffsetsSize   = (int32_t)demoSection.size();

    std::memcpy(blob.data(), &h, sizeof(h));
    return blob;
}

// -----------------------------------------------------------------------------
inline bool Metadata::Parse(const uint8_t* data, size_t size) {
    ok_ = false;
    classes_.clear();
    if (!data || size < sizeof(MetadataHeader)) return false;

    data_.assign(data, data + size);   // own the bytes

    const auto* h = reinterpret_cast<const MetadataHeader*>(data_.data()); // NOLINT
    if ((uint32_t)h->sanity != kMetadataSanity) return false;   // wrong magic
    if (h->version != kMetadataVersion) return false;           // unknown version
    version_ = h->version;

    // String table: a NUL-separated bag of names.
    if (h->stringOffset < 0 || h->stringSize <= 0 ||
        (size_t)h->stringOffset + (size_t)h->stringSize > data_.size())
        return false;
    stringTable_.assign(reinterpret_cast<const char*>(data_.data() + h->stringOffset), // NOLINT
                        (size_t)h->stringSize);
    if (stringTable_.back() != '\0') return false;   // malformed table

    const auto* typeDefs = SectionAt<TypeDefinitionV24>(
            h->typeDefinitionsOffset, h->typeDefinitionsSize, data_.size());
    const auto* fieldDefs = SectionAt<FieldDefinitionV24>(
            h->fieldsOffset, h->fieldsSize, data_.size());
    if (!typeDefs || !fieldDefs) return false;
    const size_t typeDefCount = (size_t)h->typeDefinitionsSize / sizeof(TypeDefinitionV24);
    const size_t fieldDefCount = (size_t)h->fieldsSize / sizeof(FieldDefinitionV24);

    // Demo-extension section: the "recovered" field offsets.
    // Layout: int32 classCount, then per class:
    //   int32 typeDefIndex, int32 fieldCount, fieldCount * {i32,i32,i32}.
    // We pre-scan it into a lookup table keyed by typeDefIndex — the loop over
    // type definitions below then stays simple and branch-light.
    std::vector<std::pair<int32_t, std::vector<FieldLayoutEntry>>> layoutByClass;
    if (h->demoFieldOffsetsOffset >= 0 && h->demoFieldOffsetsSize >= 12 &&
        (size_t)h->demoFieldOffsetsOffset + (size_t)h->demoFieldOffsetsSize <= data_.size()) {
        const uint8_t* p    = data_.data() + h->demoFieldOffsetsOffset;      // NOLINT
        const uint8_t* end  = p + h->demoFieldOffsetsSize;
        auto readI32 = [&](int32_t* out) {
            if (p + 4 > end) return false;
            std::memcpy(out, p, 4); p += 4; return true;
        };
        int32_t classCount = 0;
        if (readI32(&classCount) && classCount >= 0 && classCount <= 4096) {
            layoutByClass.reserve((size_t)classCount);
            for (int32_t i = 0; i < classCount; ++i) {
                int32_t idx = 0, cnt = 0;
                if (!readI32(&idx) || !readI32(&cnt)) break;
                if (cnt < 0 || p + (size_t)cnt * 12 > end) break;
                std::vector<FieldLayoutEntry> entries((size_t)cnt);
                for (int32_t f = 0; f < cnt; ++f) {
                    std::memcpy(&entries[f].nameIndex, p, 4); p += 4;
                    std::memcpy(&entries[f].offset,    p, 4); p += 4;
                    std::memcpy(&entries[f].size,      p, 4); p += 4;
                }
                layoutByClass.emplace_back(idx, std::move(entries));
            }
        }
    }
    auto findLayout = [&layoutByClass](int32_t typeDefIndex)
            -> const std::vector<FieldLayoutEntry>* {
        for (const auto& kv : layoutByClass)
            if (kv.first == typeDefIndex) return &kv.second;
        return nullptr;
    };

    classes_.reserve(typeDefCount);
    for (size_t t = 0; t < typeDefCount; ++t) {
        const TypeDefinitionV24& td = typeDefs[t];
        ClassInfo ci;
        ci.name      = StringAt(td.nameIndex);
        ci.namespaze = StringAt(td.namespaceIndex);
        ci.token     = td.token;
        ci.flags     = td.flags;
        const int32_t  start = td.fieldStart;
        const uint16_t count = td.field_count;
        if (start < 0 || (size_t)start + count > fieldDefCount) return false;

        const std::vector<FieldLayoutEntry>* layout = findLayout((int32_t)t);

        for (uint16_t f = 0; f < count; ++f) {
            const FieldDefinitionV24& fd = fieldDefs[(size_t)start + f];
            FieldInfo fi;
            fi.name      = StringAt(fd.nameIndex);
            fi.typeIndex = fd.typeIndex;
            fi.token     = fd.token;
            // Attach the matching layout entry (same ordinal) if the section
            // carried one for this class.
            if (layout && f < layout->size()) {
                fi.offset = (*layout)[f].offset;
                fi.size   = (*layout)[f].size;
            }
            ci.fields.push_back(std::move(fi));
        }
        classes_.push_back(std::move(ci));
    }
    ok_ = true;
    return true;
}

inline const ClassInfo* Metadata::FindClass(const std::string& name) const {
    for (const auto& c : classes_) if (c.name == name) return &c;
    return nullptr;
}

inline const FieldInfo* Metadata::FindField(const std::string& className,
                                            const std::string& fieldName) const {
    const ClassInfo* c = FindClass(className);
    if (!c) return nullptr;
    for (const auto& f : c->fields) if (f.name == fieldName) return &f;
    return nullptr;
}

inline std::string Metadata::Describe() const {
    std::string s;
    char line[256];
    std::snprintf(line, sizeof(line),
                  "global-metadata  magic=0x%08X  version=%d  strings=%zu B  classes=%zu\n",
                  kMetadataSanity, version_, stringTable_.size(), classes_.size());
    s += line;
    for (const auto& c : classes_) {
        std::snprintf(line, sizeof(line), "class %s.%s  (token 0x%08X, %zu fields)\n",
                      c.namespaze.c_str(), c.name.c_str(), c.token, c.fields.size());
        s += line;
        for (const auto& f : c.fields) {
            std::snprintf(line, sizeof(line),
                          "    %-16s offset=%-4d size=%-3d token=0x%08X\n",
                          f.name.c_str(), f.offset, f.size, f.token);
            s += line;
        }
    }
    return s;
}

inline std::string Metadata::ValidateAgainstGameLayout() const {
    std::string problems;
    int checked = 0;
    auto check = [&](bool good, const char* what) {
        ++checked;
        if (!good) { problems += "MISMATCH: "; problems += what; problems += "\n"; }
    };

    // The parsed offsets must describe the compiled structs exactly, or every
    // address the reader computes from them would be wrong. This is the same
    // discipline as "the dump must match the exact game build" — automated
    // here at startup, and surfaced in the overlay's Misc tab.
    const ClassInfo* root = FindClass("GameRoot");
    check(root != nullptr, "class GameRoot missing from metadata");
    if (root) {
        check(root->fields.size() == 9, "GameRoot field count");
        auto f = [&](const char* n) { return FindField("GameRoot", n); };
        check(f("header")   && f("header")->offset   == (int32_t)offsetof(demo::GameRoot, header),   "GameRoot.header offset");
        check(f("player")   && f("player")->offset   == (int32_t)offsetof(demo::GameRoot, player),   "GameRoot.player offset");
        check(f("vehicles") && f("vehicles")->offset == (int32_t)offsetof(demo::GameRoot, vehicles), "GameRoot.vehicles offset");
        check(f("ores")     && f("ores")->offset     == (int32_t)offsetof(demo::GameRoot, ores),     "GameRoot.ores offset");
        check(f("workers")  && f("workers")->offset  == (int32_t)offsetof(demo::GameRoot, workers),  "GameRoot.workers offset");
        check(f("economy")  && f("economy")->offset  == (int32_t)offsetof(demo::GameRoot, economy),  "GameRoot.economy offset");
        check(f("settings") && f("settings")->offset == (int32_t)offsetof(demo::GameRoot, settings), "GameRoot.settings offset");
    }

    const ClassInfo* player = FindClass("Player");
    check(player != nullptr, "class Player missing from metadata");
    if (player) {
        check(player->fields.size() == 8, "Player field count");
        auto pfo = [&](const char* n) -> int32_t {
            const FieldInfo* fi = FindField("Player", n);
            return fi ? fi->offset : -1;
        };
        check(pfo("health") == (int32_t)offsetof(demo::Player, health), "Player.health offset");
        check(pfo("xp")     == (int32_t)offsetof(demo::Player, xp),     "Player.xp offset");
    }

    const ClassInfo* economy = FindClass("Economy");
    check(economy != nullptr, "class Economy missing from metadata");
    if (economy) {
        const FieldInfo* coins = FindField("Economy", "coins");
        check(coins && coins->offset == (int32_t)offsetof(demo::Economy, coins), "Economy.coins offset");
        check(coins && coins->size   == 8,                                       "Economy.coins size (double)");
    }

    if (problems.empty()) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "OK — %d layout checks passed (metadata ↔ compiled structs)\n", checked);
        return buf;
    }
    return problems;
}

} // namespace il2cpp
