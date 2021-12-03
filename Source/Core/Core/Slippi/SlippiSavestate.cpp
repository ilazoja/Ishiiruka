#include "SlippiSavestate.h"
#include "Common/CommonFuncs.h"
#include "Common/MemoryUtil.h"
#include "Core/HW/AudioInterface.h"
#include "Core/HW/DSP.h"
#include "Core/HW/DVDInterface.h"
#include "Core/HW/EXI.h"
#include "Core/HW/GPFifo.h"
#include "Core/HW/HW.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/HW/SI.h"
#include "Core/HW/VideoInterface.h"
#include "Core/PowerPC/PowerPC.h"
#include <vector>

bool SlippiSavestate::shouldForceInit;

SlippiSavestate::SlippiSavestate()
{
	initBackupLocs();

	for (auto it = backupLocs.begin(); it != backupLocs.end(); ++it)
	{
		auto size = it->endAddress - it->startAddress;
		it->data = static_cast<u8 *>(Common::AllocateAlignedMemory(size, 64));
	}

	// u8 *ptr = nullptr;
	// PointerWrap p(&ptr, PointerWrap::MODE_MEASURE);

	// getDolphinState(p);
	// const size_t buffer_size = reinterpret_cast<size_t>(ptr);
	// dolphinSsBackup.resize(buffer_size);
}

SlippiSavestate::~SlippiSavestate()
{
	for (auto it = backupLocs.begin(); it != backupLocs.end(); ++it)
	{
		Common::FreeAlignedMemory(it->data);
	}
}

bool cmpFn(SlippiSavestate::PreserveBlock pb1, SlippiSavestate::PreserveBlock pb2)
{
	return pb1.address < pb2.address;
}

void SlippiSavestate::initBackupLocs()
{
	static std::vector<ssBackupLoc> fullBackupRegions = {
	    {0x8123ab60, 0x8128cb60, nullptr}, // Fighter 1 instance
	    {0x8128cb60, 0x812deb60, nullptr}, // Fighter 2 instance
	    {0x812deb60, 0x81330b60, nullptr}, // Fighter 3 instance
	    {0x81330b60, 0x81382b60, nullptr}, // Fighter 4 instance
	    {0x81382b60, 0x814ce460, nullptr}, // Item instance
	    {0x814ce460, 0x8154e560, nullptr}, // Stage instance
	    {0x8154e560, 0x81601960, nullptr}  // Physics instance
	};

	static std::vector<PreserveBlock> excludeSections = {};

	static std::vector<ssBackupLoc> processedLocs = {};

	// If the processed locations are already computed, just copy them directly
	if (processedLocs.size() && !shouldForceInit)
	{
		backupLocs.insert(backupLocs.end(), processedLocs.begin(), processedLocs.end());
		return;
	}

	shouldForceInit = false;

	// Sort exclude sections
	std::sort(excludeSections.begin(), excludeSections.end(), cmpFn);

	// Initialize backupLocs to full regions
	backupLocs.insert(backupLocs.end(), fullBackupRegions.begin(), fullBackupRegions.end());

	// Remove exclude sections from backupLocs
	int idx = 0;
	for (auto it = excludeSections.begin(); it != excludeSections.end(); ++it)
	{
		PreserveBlock ipb = *it;

		while (ipb.length > 0)
		{
			// Move up the backupLocs index until we reach a section relevant to us
			while (idx < backupLocs.size() && ipb.address >= backupLocs[idx].endAddress)
			{
				idx += 1;
			}

			// Once idx is beyond backup locs, we are already not backup up this exclusion section
			if (idx >= backupLocs.size())
			{
				break;
			}

			// Handle case where our exclusion starts before the actual backup section
			if (ipb.address < backupLocs[idx].startAddress)
			{
				int newSize = (s32)ipb.length - ((s32)backupLocs[idx].startAddress - (s32)ipb.address);

				ipb.length = newSize > 0 ? newSize : 0;
				ipb.address = backupLocs[idx].startAddress;
				continue;
			}

			// Determine new size (how much we removed from backup)
			int newSize = (s32)ipb.length - ((s32)backupLocs[idx].endAddress - (s32)ipb.address);

			// Add split section after exclusion
			if (backupLocs[idx].endAddress > ipb.address + ipb.length)
			{
				ssBackupLoc newLoc = {ipb.address + ipb.length, backupLocs[idx].endAddress, nullptr};
				backupLocs.insert(backupLocs.begin() + idx + 1, newLoc);
			}

			// Modify section to end at the exclusion start
			backupLocs[idx].endAddress = ipb.address;
			if (backupLocs[idx].endAddress <= backupLocs[idx].startAddress)
			{
				backupLocs.erase(backupLocs.begin() + idx);
			}

			// Set new size to see if there's still more to process
			newSize = newSize > 0 ? newSize : 0;
			ipb.address = ipb.address + (ipb.length - newSize);
			ipb.length = (u32)newSize;
		}
	}

	processedLocs.clear();
	processedLocs.insert(processedLocs.end(), backupLocs.begin(), backupLocs.end());
}

void SlippiSavestate::getDolphinState(PointerWrap &p)
{
	// p.DoArray(Memory::m_pRAM, Memory::RAM_SIZE);
	// p.DoMarker("Memory");
	// VideoInterface::DoState(p);
	// p.DoMarker("VideoInterface");
	// SerialInterface::DoState(p);
	// p.DoMarker("SerialInterface");
	// ProcessorInterface::DoState(p);
	// p.DoMarker("ProcessorInterface");
	// DSP::DoState(p);
	// p.DoMarker("DSP");
	// DVDInterface::DoState(p);
	// p.DoMarker("DVDInterface");
	// GPFifo::DoState(p);
	// p.DoMarker("GPFifo");
	ExpansionInterface::DoState(p);
	p.DoMarker("ExpansionInterface");
	// AudioInterface::DoState(p);
	// p.DoMarker("AudioInterface");
}

void SlippiSavestate::Capture()
{
	// First copy memory
	for (auto it = backupLocs.begin(); it != backupLocs.end(); ++it)
	{
		auto size = it->endAddress - it->startAddress;
		Memory::CopyFromEmu(it->data, it->startAddress, size);
	}

	//// Second copy dolphin states
	// u8 *ptr = &dolphinSsBackup[0];
	// PointerWrap p(&ptr, PointerWrap::MODE_WRITE);
	// getDolphinState(p);
}

void SlippiSavestate::Load(std::vector<PreserveBlock> blocks)
{
	// static std::vector<PreserveBlock> interruptStuff = {
	//    {0x804BF9D2, 4},
	//    {0x804C3DE4, 20},
	//    {0x804C4560, 44},
	//    {0x804D7760, 36},
	//};

	// for (auto it = interruptStuff.begin(); it != interruptStuff.end(); ++it)
	// {
	//  blocks.push_back(*it);
	// }

	// Back up
	for (auto it = blocks.begin(); it != blocks.end(); ++it)
	{
		if (!preservationMap.count(*it))
		{
			// TODO: Clear preservation map when game ends
			preservationMap[*it] = std::vector<u8>(it->length);
		}

		Memory::CopyFromEmu(&preservationMap[*it][0], it->address, it->length);
	}

	// Restore memory blocks
	for (auto it = backupLocs.begin(); it != backupLocs.end(); ++it)
	{
		auto size = it->endAddress - it->startAddress;
		Memory::CopyToEmu(it->startAddress, it->data, size);
	}

	//// Restore audio
	// u8 *ptr = &dolphinSsBackup[0];
	// PointerWrap p(&ptr, PointerWrap::MODE_READ);
	// getDolphinState(p);

	// Restore
	for (auto it = blocks.begin(); it != blocks.end(); ++it)
	{
		Memory::CopyToEmu(it->address, &preservationMap[*it][0], it->length);
	}
}
