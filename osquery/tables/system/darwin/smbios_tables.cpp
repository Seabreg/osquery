/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include <osquery/tables.h>

#include "osquery/core/conversions.h"
#include "osquery/core/md5.h"

namespace osquery {
namespace tables {

#define kIOSMBIOSClassName_ "AppleSMBIOS"
#define kIOSMBIOSPropertyName_ "SMBIOS"
#define kIOSMBIOSEPSPropertyName_ "SMBIOS-EPS"

typedef struct SMBStructHeader {
  uint8_t type;
  uint8_t length;
  uint16_t handle;
} __attribute__((packed)) SMBStructHeader;

const std::map<int, std::string> kSMBIOSTypeDescriptions = {
    {0, "BIOS Information"},
    {1, "System Information"},
    {2, "Base Board or Module Information"},
    {3, "System Enclosure or Chassis"},
    {4, "Processor Information"},
    {5, "Memory Controller Information"},
    {6, "Memory Module Information"},
    {7, "Cache Information"},
    {8, "Port Connector Information"},
    {9, "System Slots"},
    {10, "On Board Devices Information"},
    {11, "OEM Strings"},
    {12, "System Configuration Options"},
    {13, "BIOS Language Information"},
    {14, "Group Associations"},
    {15, "System Event Log"},
    {16, "Physical Memory Array"},
    {17, "Memory Device"},
    {18, "32-bit Memory Error Information"},
    {19, "Memory Array Mapped Address"},
    {20, "Memory Device Mapped Address"},
    {21, "Built-in Pointing Device"},
    {22, "Portable Battery"},
    {23, "System Reset"},
    {24, "Hardware Security"},
    {25, "System Power Controls"},
    {26, "Voltage Probe"},
    {27, "Cooling Device"},
    {28, "Temperature Probe"},
    {29, "Electrical Current Probe"},
    {30, "Out-of-Band Remote Access"},
    {31, "Boot Integrity Services"},
    {32, "System Boot Information"},
    {33, "64-bit Memory Error Information"},
    {34, "Management Device"},
    {35, "Management Device Component"},
    {36, "Management Device Threshold Data"},
    {37, "Memory Channel"},
    {38, "IPMI Device Information"},
    {39, "System Power Supply"},
    {40, "Additional Information"},
    {41, "Onboard Devices Extended Info"},
    {126, "Inactive"},
    {127, "End-of-Table"},
    {130, "Memory SPD Data"},
    {131, "OEM Processor Type"},
    {132, "OEM Processor Bus Speed"},
};

void genSMBIOSTables(const uint8_t* tables, size_t length, QueryData& results) {
  // Keep a pointer to the end of the SMBIOS data for comparison.
  auto tables_end = tables + length;
  auto table = tables;

  // Iterate through table structures within SMBIOS data range.
  size_t index = 0;
  while (table + sizeof(SMBStructHeader) <= tables_end) {
    auto header = (const SMBStructHeader*)table;
    if (table + header->length > tables_end) {
      // Invalid header, length must be within SMBIOS data range.
      break;
    }

    Row r;
    // The index is a supliment that keeps track of table order.
    r["number"] = INTEGER(index++);
    r["type"] = INTEGER((unsigned short)header->type);
    if (kSMBIOSTypeDescriptions.count(header->type) > 0) {
      r["description"] = kSMBIOSTypeDescriptions.at(header->type);
    }

    r["handle"] = BIGINT((unsigned long long)header->handle);
    r["header_size"] = INTEGER((unsigned short)header->length);

    // The SMBIOS structure may have unformatted, double-NULL delimited trailing
    // data, which are usually strings.
    auto next_table = table + header->length;
    for (; next_table + sizeof(SMBStructHeader) <= tables_end; next_table++) {
      if (next_table[0] == 0 && next_table[1] == 0) {
        next_table += 2;
        break;
      }
    }

    auto table_length = next_table - table;
    r["size"] = INTEGER(table_length);

    md5::MD5 digest;
    auto md5_digest = digest.digestMemory(table, table_length);
    r["md5"] = std::string(md5_digest);

    table = next_table;
    results.push_back(r);
  }
}

QueryData genSMBIOSTables(QueryContext& context) {
  QueryData results;

  auto matching = IOServiceMatching(kIOSMBIOSClassName_);
  if (matching == nullptr) {
    // No ACPI platform expert service found.
    return {};
  }

  auto service = IOServiceGetMatchingService(kIOMasterPortDefault, matching);
  if (service == 0) {
    return {};
  }

  // Unlike ACPI the SMBIOS property will return several structures
  // followed by a table of structured entries (also called tables).
  // http://dmtf.org/sites/default/files/standards/documents/DSP0134_2.8.0.pdf
  CFTypeRef smbios = IORegistryEntryCreateCFProperty(
      service, CFSTR(kIOSMBIOSPropertyName_), kCFAllocatorDefault, 0);
  if (smbios == nullptr) {
    IOObjectRelease(service);
    return {};
  }

  // Check the first few SMBIOS structures before iterating through tables.
  const uint8_t* smbios_data = CFDataGetBytePtr((CFDataRef)smbios);
  size_t length = CFDataGetLength((CFDataRef)smbios);

  if (smbios_data == nullptr || length == 0) {
    // Problem creating SMBIOS property.
    IOObjectRelease(service);
    return {};
  }

  // Parse structures.
  genSMBIOSTables(smbios_data, length, results);

  CFRelease(smbios);
  IOObjectRelease(service);
  return results;
}
}
}
