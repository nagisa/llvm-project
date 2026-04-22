//===-- SBFAsmBackend.cpp - SBF Assembler Backend -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SBFMCTargetDesc.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/EndianStream.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

namespace {

class SBFAsmBackend : public MCAsmBackend {
public:
  SBFAsmBackend(endianness Endian, const MCSubtargetInfo &STI)
      : MCAsmBackend(Endian),
        relocAbs64(STI.hasFeature(SBF::FeatureRelocAbs64)) {}
  ~SBFAsmBackend() override = default;

  void applyFixup(const MCFragment &, const MCFixup &, const MCValue &Target,
                  uint8_t *Data, uint64_t Value, bool IsResolved) override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override;

  // No instruction requires relaxation
  bool fixupNeedsRelaxation(const MCFixup &Fixup, uint64_t Value)
                                                    const override {
    return false;
  }


  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;
private:
  bool relocAbs64;
};

} // end anonymous namespace

bool SBFAsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                 const MCSubtargetInfo *STI) const {
  if ((Count % 8) != 0)
    return false;

  for (uint64_t i = 0; i < Count; i += 8)
    support::endian::write<uint64_t>(OS, 0x15000000, Endian);

  return true;
}

void SBFAsmBackend::applyFixup(const MCFragment &F, const MCFixup &Fixup,
                               const MCValue &Target, uint8_t *Data,
                               uint64_t Value, bool IsResolved) {
  maybeAddReloc(F, Fixup, Target, Value, IsResolved);
  if (Fixup.getKind() == FK_SecRel_8) {
    // The Value is 0 for global variables, and the in-section offset
    // for static variables. Write to the immediate field of the inst.
    assert(Value <= UINT32_MAX);
    support::endian::write<uint32_t>(Data + 4, static_cast<uint32_t>(Value),
                                     Endian);
  } else if (Fixup.getKind() == FK_Data_4 && !Fixup.isPCRel()) {
    support::endian::write<uint32_t>(Data, Value, Endian);
  } else if (Fixup.getKind() == FK_Data_8) {
    support::endian::write<uint64_t>(Data, Value, Endian);
  } else if (Fixup.getKind() == FK_Data_4 && Fixup.isPCRel()) {
    Value = (uint32_t)((Value - 8) / 8);
    if (Endian == endianness::little) {
      Data[1] = 0x10;
      support::endian::write32le(Data + 4, Value);
    } else {
      Data[1] = 0x1;
      support::endian::write32be(Data + 4, Value);
    }
  } else {
    assert(Fixup.getKind() == FK_Data_2 && Fixup.isPCRel());

    int64_t ByteOff = (int64_t)Value - 8;
    if (ByteOff > INT16_MAX * 8 || ByteOff < INT16_MIN * 8)
      report_fatal_error("Branch target out of insn range");

    Value = (uint16_t)((Value - 8) / 8);
    support::endian::write<uint16_t>(Data + 2, Value,
                                     Endian);
  }
}

std::unique_ptr<MCObjectTargetWriter>
SBFAsmBackend::createObjectTargetWriter() const {
  return createSBFELFObjectWriter(0, relocAbs64);
}

MCAsmBackend *llvm::createSBFAsmBackend(const Target &T,
                                        const MCSubtargetInfo &STI,
                                        const MCRegisterInfo &MRI,
                                        const MCTargetOptions &) {
  return new SBFAsmBackend(endianness::little, STI);
}

MCAsmBackend *llvm::createSBFbeAsmBackend(const Target &T,
                                          const MCSubtargetInfo &STI,
                                          const MCRegisterInfo &MRI,
                                          const MCTargetOptions &) {
  return new SBFAsmBackend(endianness::big, STI);
}
