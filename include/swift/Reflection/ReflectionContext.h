//===--- ReflectionContext.h - Swift Type Reflection Context ----*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// Implements the context for allocations and management of structures related
// to reflection, such as TypeRefs.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_REFLECTION_REFLECTIONCONTEXT_H
#define SWIFT_REFLECTION_REFLECTIONCONTEXT_H

#include "swift/Runtime/Metadata.h"
#include "swift/Reflection/Reader.h"
#include "swift/Reflection/Records.h"
#include "swift/Reflection/TypeRef.h"

#include <iostream>
#include <vector>
#include <unordered_map>

class NodePointer;

namespace swift {
namespace reflection {

template <typename Runtime>
using SharedTargetMetadataRef = std::shared_ptr<TargetMetadata<Runtime>>;

template <typename Runtime>
using SharedTargetNominalTypeDescriptorRef
  = std::shared_ptr<TargetNominalTypeDescriptor<Runtime>>;

template <typename Runtime>
using SharedProtocolDescriptorRef
  = std::shared_ptr<TargetProtocolDescriptor<Runtime>>;

using FieldSection = ReflectionSection<FieldDescriptorIterator>;
using AssociatedTypeSection = ReflectionSection<AssociatedTypeIterator>;
using GenericSection = ReflectionSection<const void *>;

struct ReflectionInfo {
  std::string ImageName;
  FieldSection fieldmd;
  AssociatedTypeSection assocty;
  GenericSection reflstr;
  GenericSection typeref;
};

template <typename Runtime>
class ReflectionContext {
  using StoredPointer = typename Runtime::StoredPointer;
  using StoredSize = typename Runtime::StoredSize;

  std::vector<ReflectionInfo> ReflectionInfos;
  std::unordered_map<StoredPointer, TypeRefPointer> TypeRefCache;
  MemoryReader &Reader;

  void dumpTypeRef(const std::string &MangledName,
                   std::ostream &OS, bool printTypeName = false) const {
    auto TypeName = Demangle::demangleTypeAsString(MangledName);
    auto DemangleTree = Demangle::demangleTypeAsNode(MangledName);
    auto TR = TypeRef::fromDemangleNode(DemangleTree);
    OS << TypeName << '\n';
    TR->dump(OS);
    std::cout << std::endl;
  }

  template <typename M>
  SharedTargetMetadataRef<Runtime> _readMetadata(StoredPointer Address,
                                                 size_t Size = sizeof(M)) {
    uint8_t *Buffer = (uint8_t *)malloc(Size);
    if (!Reader.readBytes(Address, Buffer, Size)) {
      free(Buffer);
      return nullptr;
    }

    auto Casted = reinterpret_cast<TargetMetadata<Runtime> *>(Buffer);
    return SharedTargetMetadataRef<Runtime>(Casted,
                                            [](void *Meta){
                                              free((void*)Meta);
                                            });
  }
  

public:
  ReflectionContext(MemoryReader &Reader) : Reader(Reader) {}

  void dumpFieldSection(std::ostream &OS) const {
    for (const auto &sections : ReflectionInfos) {
      for (const auto &descriptor : sections.fieldmd) {
        dumpTypeRef(descriptor.getMangledTypeName(), OS);
        for (auto &field : descriptor) {
          OS << field.getFieldName() << ": ";
          dumpTypeRef(field.getMangledTypeName(), OS);
        }
      }
    }
  }

  void dumpAssociatedTypeSection(std::ostream &OS) const {
    for (const auto &sections : ReflectionInfos) {
      for (const auto &descriptor : sections.assocty) {
        auto conformingTypeName = Demangle::demangleTypeAsString(
          descriptor.getMangledConformingTypeName());
        auto protocolName = Demangle::demangleTypeAsString(
          descriptor.getMangledProtocolTypeName());

        OS << conformingTypeName << " : " << protocolName;
        OS << std::endl;

        for (const auto &associatedType : descriptor) {
          OS << "typealias " << associatedType.getName() << " = ";
          dumpTypeRef(associatedType.getMangledSubstitutedTypeName(), OS);
        }
      }
    }
  }

  void dumpAllSections(std::ostream &OS) const {
    OS << "FIELDS:\n";
    for (size_t i = 0; i < 7; ++i) OS << '=';
    OS << std::endl;
    dumpFieldSection(OS);
    OS << "\nASSOCIATED TYPES:\n";
    for (size_t i = 0; i < 17; ++i) OS << '=';
    OS << std::endl;
    dumpAssociatedTypeSection(OS);
    OS << std::endl;
  }

  SharedTargetMetadataRef<Runtime> readMetadata(StoredPointer Address) {
    StoredPointer KindValue = 0;
    if (!Reader.readInteger(Address, &KindValue))
      return nullptr;

    auto Kind = static_cast<MetadataKind>(KindValue);

    if (metadataKindIsClass(Kind)) {
      return _readMetadata<TargetClassMetadata<Runtime>>(Address);
    } else {
      switch (Kind) {
      case MetadataKind::Enum:
        return _readMetadata<TargetEnumMetadata<Runtime>>(Address);
      case MetadataKind::ErrorObject:
        return _readMetadata<TargetEnumMetadata<Runtime>>(Address);
      case MetadataKind::Existential: {
        StoredPointer NumProtocolsAddress = Address +
          TargetExistentialTypeMetadata<Runtime>::OffsetToNumProtocols;
        StoredPointer NumProtocols;
        if (!Reader.readInteger(NumProtocolsAddress, &NumProtocols))
          return nullptr;

        auto TotalSize = sizeof(TargetExistentialTypeMetadata<Runtime>) +
          NumProtocols *
            sizeof(ConstTargetMetadataPointer<Runtime, TargetProtocolDescriptor>);
        
        return _readMetadata<TargetExistentialTypeMetadata<Runtime>>(Address,
                                                                     TotalSize);
      }
      case MetadataKind::ExistentialMetatype:
        return _readMetadata<
          TargetExistentialMetatypeMetadata<Runtime>>(Address);
      case MetadataKind::ForeignClass:
        return _readMetadata<TargetForeignClassMetadata<Runtime>>(Address);
      case MetadataKind::Function:
        return _readMetadata<TargetFunctionTypeMetadata<Runtime>>(Address);
      case MetadataKind::HeapGenericLocalVariable:
        return _readMetadata<TargetHeapLocalVariableMetadata<Runtime>>(Address);
      case MetadataKind::HeapLocalVariable:
        return _readMetadata<TargetHeapLocalVariableMetadata<Runtime>>(Address);
      case MetadataKind::Metatype:
        return _readMetadata<TargetMetatypeMetadata<Runtime>>(Address);
      case MetadataKind::ObjCClassWrapper:
        return _readMetadata<TargetObjCClassWrapperMetadata<Runtime>>(Address);
      case MetadataKind::Opaque:
        return _readMetadata<TargetOpaqueMetadata<Runtime>>(Address);
      case MetadataKind::Optional:
        return _readMetadata<TargetEnumMetadata<Runtime>>(Address);
      case MetadataKind::Struct:
        return _readMetadata<TargetStructMetadata<Runtime>>(Address);
      case MetadataKind::Tuple: {
        auto NumElementsAddress = Address +
          TargetTupleTypeMetadata<Runtime>::OffsetToNumElements;
        StoredSize NumElements;
        if (!Reader.readInteger(NumElementsAddress, &NumElements))
          return nullptr;
        auto TotalSize = sizeof(TargetTupleTypeMetadata<Runtime>) +
          NumElements * sizeof(StoredPointer);
        return _readMetadata<TargetTupleTypeMetadata<Runtime>>(Address,
                                                               TotalSize);
      }
      default:
        return nullptr;
      }
    }
  }

  template<typename Offset>
  StoredPointer resolveRelativeOffset(StoredPointer targetAddress) {
    Offset relative;
    if (!Reader.readInteger(targetAddress, &relative))
      return 0;
    using SignedOffset = typename std::make_signed<Offset>::type;
    using SignedPointer = typename std::make_signed<StoredPointer>::type;
    auto signext = (SignedPointer)(SignedOffset)relative;
    return targetAddress + signext;
  }

  SharedTargetNominalTypeDescriptorRef<Runtime>
  readNominalTypeDescriptor(StoredPointer Address) {
    auto Size = sizeof(TargetNominalTypeDescriptor<Runtime>);
    auto Buffer = (uint8_t *)malloc(Size);
    if (!Reader.readBytes(Address, Buffer, Size)) {
      free(Buffer);
      return nullptr;
    }

    auto Casted
      = reinterpret_cast<TargetNominalTypeDescriptor<Runtime> *>(Buffer);
    return SharedTargetNominalTypeDescriptorRef<Runtime>(Casted,
                                            [](void *NTD){
                                              free(NTD);
                                            });
  }

  SharedProtocolDescriptorRef<Runtime>
  readProtocolDescriptor(StoredPointer Address) {
    auto Size = sizeof(TargetProtocolDescriptor<Runtime>);
    auto Buffer = (uint8_t *)malloc(Size);
    if (!Reader.readBytes(Address, Buffer, Size)) {
      free(Buffer);
      return nullptr;
    }
    auto Casted
      = reinterpret_cast<TargetProtocolDescriptor<Runtime> *>(Buffer);
    return SharedProtocolDescriptorRef<Runtime>(Casted,
                                                      [](void *PD){
                                                        free(PD);
                                                      });
  }

  TypeRefVector
  getGenericArguments(StoredPointer MetadataAddress,
                      SharedTargetNominalTypeDescriptorRef<Runtime> Descriptor){
    TypeRefVector GenericArgTypeRefs;
    auto NumGenericParams = Descriptor->GenericParams.NumPrimaryParams;
    auto OffsetToGenericArgs
      = sizeof(StoredPointer) * (Descriptor->GenericParams.Offset);
    auto AddressOfGenericArgAddress = MetadataAddress + OffsetToGenericArgs;

    using ArgIndex = decltype(Descriptor->GenericParams.NumPrimaryParams);
    for (ArgIndex i = 0; i < NumGenericParams; ++i,
         AddressOfGenericArgAddress += sizeof(StoredPointer)) {
        StoredPointer GenericArgAddress;
        if (!Reader.readInteger(AddressOfGenericArgAddress,
                                &GenericArgAddress))
          return {};
      if (auto GenericArg = getTypeRef(GenericArgAddress))
        GenericArgTypeRefs.push_back(GenericArg);
      else
        return {};
      }
    return GenericArgTypeRefs;
  }

  TypeRefPointer
  resolveDependentMembers(TypeRefPointer Unresolved,
                          StoredPointer MetadataAddress) {
    // TODO: Resolve dependent members
    return Unresolved;
  }

  TypeRefPointer
  getNominalTypeRef(StoredPointer MetadataAddress,
                    StoredPointer DescriptorAddress) {
    auto Descriptor = readNominalTypeDescriptor(DescriptorAddress);
    if (!Descriptor)
      return nullptr;

    auto NameAddress
      = resolveRelativeOffset<int32_t>(DescriptorAddress +
                                       Descriptor->offsetToNameOffset());
    auto MangledName = Reader.readString(NameAddress);
    if (MangledName.empty())
      return nullptr;

    auto DemangleNode = Demangle::demangleTypeAsNode(MangledName);
    if (!DemangleNode)
      return nullptr;

    TypeRefPointer Nominal;
    if (Descriptor->GenericParams.NumPrimaryParams) {
      auto Args = getGenericArguments(MetadataAddress, Descriptor);
      Nominal = BoundGenericTypeRef::create(MangledName, Args);
    } else {
      Nominal = TypeRef::fromDemangleNode(DemangleNode);
    }
    TypeRefCache.insert({MetadataAddress, Nominal});
    return Nominal;
  }

  TypeRefPointer getTypeRef(StoredPointer MetadataAddress) {
    auto Cached = TypeRefCache.find(MetadataAddress);
    if (Cached != TypeRefCache.end())
      return Cached->second;

    auto Meta = readMetadata(MetadataAddress);
    if (!Meta) return nullptr;

    switch (Meta->getKind()) {
    case MetadataKind::Class: {
      auto ClassMeta = llvm::cast<TargetClassMetadata<Runtime>>(Meta.get());
      if (ClassMeta->isPureObjC())
        return ObjCClassTypeRef::Unnamed;

      auto DescriptorAddress
        = resolveRelativeOffset<StoredPointer>(MetadataAddress +
                                         ClassMeta->offsetToDescriptorOffset());

      return getNominalTypeRef(MetadataAddress, DescriptorAddress);
    }
    case MetadataKind::Struct: {
      auto StructMeta = cast<TargetStructMetadata<Runtime>>(Meta.get());

      auto DescriptorAddress
        = resolveRelativeOffset<StoredPointer>(MetadataAddress +
                                        StructMeta->offsetToDescriptorOffset());
      return getNominalTypeRef(MetadataAddress, DescriptorAddress);
    }
    case MetadataKind::Enum:
    case MetadataKind::Optional: {
      auto EnumMeta = cast<TargetEnumMetadata<Runtime>>(Meta.get());
      auto DescriptorAddress
        = resolveRelativeOffset<StoredPointer>(MetadataAddress +
                                          EnumMeta->offsetToDescriptorOffset());
      return getNominalTypeRef(MetadataAddress, DescriptorAddress);
    }
    case MetadataKind::Tuple: {
      auto TupleMeta = cast<TargetTupleTypeMetadata<Runtime>>(Meta.get());
      TypeRefVector Elements;
      StoredPointer ElementAddress = MetadataAddress +
        sizeof(TargetTupleTypeMetadata<Runtime>);
      using Element = typename TargetTupleTypeMetadata<Runtime>::Element;
      for (StoredPointer i = 0; i < TupleMeta->NumElements; ++i,
           ElementAddress += sizeof(Element)) {
        Element E;
        if (!Reader.readBytes(ElementAddress, (uint8_t*)&E, sizeof(Element)))
          return nullptr;

        if (auto ElementTypeRef = getTypeRef(E.Type))
          Elements.push_back(ElementTypeRef);
        else
          return nullptr;
      }
      return TupleTypeRef::create(Elements);
    }
    case MetadataKind::Function: {
      auto Function = cast<TargetFunctionTypeMetadata<Runtime>>(Meta.get());
      StoredPointer FlagsAddress = MetadataAddress +
        TargetFunctionTypeMetadata<Runtime>::OffsetToFlags;
      TargetFunctionTypeFlags<Runtime> Flags;
      if (!Reader.readBytes(FlagsAddress, (uint8_t*)&Flags, sizeof(Flags)))
        return nullptr;
      TypeRefVector Arguments;
      StoredPointer ArgumentAddress = MetadataAddress +
        sizeof(TargetFunctionTypeMetadata<Runtime>);
      for (StoredPointer i = 0; i < Function->getNumArguments(); ++i,
           ArgumentAddress += sizeof(StoredPointer)) {
        StoredPointer FlaggedArgumentAddress;
        if (!Reader.readInteger(ArgumentAddress, &FlaggedArgumentAddress))
          return nullptr;
        // TODO: Use target-agnostic FlaggedPointer to mask this!
        FlaggedArgumentAddress &= ~((StoredPointer)1);
        if (auto ArgumentTypeRef = getTypeRef(FlaggedArgumentAddress))
          Arguments.push_back(ArgumentTypeRef);
        else
          return nullptr;
      }

      auto Result = getTypeRef(Function->ResultType);
      return FunctionTypeRef::create(Arguments, Result);
    }
    case MetadataKind::Existential: {
      auto Exist = cast<TargetExistentialTypeMetadata<Runtime>>(Meta.get());
      TypeRefVector Protocols;
      for (size_t i = 0; i < Exist->Protocols.NumProtocols; ++i) {
        auto ProtocolAddress = Exist->Protocols[i];
        auto ProtocolDescriptor = readProtocolDescriptor(ProtocolAddress);
        if (!ProtocolDescriptor)
          return nullptr;
        auto MangledName = Reader.readString(ProtocolDescriptor->Name);
        if (MangledName.empty())
          return nullptr;
        auto Demangled = Demangle::demangleSymbolAsNode(MangledName);
        auto Protocol = TypeRef::fromDemangleNode(Demangled);
        if (!llvm::isa<ProtocolTypeRef>(Protocol.get()))
          return nullptr;

        Protocols.push_back(Protocol);
      }
      return ProtocolCompositionTypeRef::create(Protocols);
    }
    case MetadataKind::Metatype: {
      auto Metatype = cast<TargetMetatypeMetadata<Runtime>>(Meta.get());
      auto Instance = getTypeRef(Metatype->InstanceType);
      return MetatypeTypeRef::create(Instance);
    }
    case MetadataKind::ObjCClassWrapper:
      return ObjCClassTypeRef::Unnamed;
    case MetadataKind::ExistentialMetatype: {
      auto Exist = cast<TargetExistentialMetatypeMetadata<Runtime>>(Meta.get());
      auto Instance = getTypeRef(Exist->InstanceType);
      return ExistentialMetatypeTypeRef::create(Instance);
    }
    case MetadataKind::ForeignClass:
      return ForeignClassTypeRef::Unnamed;
    case MetadataKind::HeapLocalVariable:
      return ForeignClassTypeRef::Unnamed;
    case MetadataKind::HeapGenericLocalVariable:
      return ForeignClassTypeRef::Unnamed;
    case MetadataKind::ErrorObject:
      return ForeignClassTypeRef::Unnamed;
    case MetadataKind::Opaque:
      return OpaqueTypeRef::Opaque;
    }
  }

  void clear() {
    TypeRefCache.clear();
  }

  void addReflectionInfo(ReflectionInfo I) {
    ReflectionInfos.push_back(I);
  }
};

} // end namespace reflection
} // end namespace swift

#endif // SWIFT_REFLECTION_REFLECTIONCONTEXT_H
