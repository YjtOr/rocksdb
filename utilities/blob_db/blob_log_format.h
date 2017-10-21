//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// Log format information shared by reader and writer.

#pragma once

#ifndef ROCKSDB_LITE

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include "rocksdb/options.h"
#include "rocksdb/status.h"
#include "rocksdb/types.h"

namespace rocksdb {

namespace blob_db {
class BlobFile;
class BlobDBImpl;

constexpr uint64_t kNoExpiration = std::numeric_limits<uint64_t>::max();

enum RecordType : uint8_t {
  // Zero is reserved for preallocated files
  kFullType = 0,

  // For fragments
  kFirstType = 1,
  kMiddleType = 2,
  kLastType = 3,
  kMaxRecordType = kLastType
};

enum RecordSubType : uint8_t {
  kRegularType = 0,
  kTTLType = 1,
  kTimestampType = 2,
};

extern const uint32_t kMagicNumber;

class Reader;

using ttlrange_t = std::pair<uint64_t, uint64_t>;
using tsrange_t = std::pair<uint64_t, uint64_t>;
using snrange_t = std::pair<rocksdb::SequenceNumber, rocksdb::SequenceNumber>;

class BlobLogHeader {
  friend class BlobFile;
  friend class BlobDBImpl;

 private:
  uint32_t magic_number_ = 0;
  uint32_t version_ = 1;
  CompressionType compression_;
  std::unique_ptr<ttlrange_t> ttl_guess_;
  std::unique_ptr<tsrange_t> ts_guess_;

 private:
  void set_ttl_guess(const ttlrange_t& ttl) {
    ttl_guess_.reset(new ttlrange_t(ttl));
  }

  void set_version(uint32_t v) { version_ = v; }

  void set_ts_guess(const tsrange_t& ts) { ts_guess_.reset(new tsrange_t(ts)); }

 public:
  // magic number + version + flags + ttl guess + timestamp range = 44
  static const size_t kHeaderSize = 4 + 4 + 4 + 8 * 2 + 8 * 2;

  void EncodeTo(std::string* dst) const;

  Status DecodeFrom(const Slice& input);

  BlobLogHeader();

  uint32_t magic_number() const { return magic_number_; }

  uint32_t version() const { return version_; }

  CompressionType compression() const { return compression_; }

  ttlrange_t ttl_range() const {
    if (!ttl_guess_) {
      return {0, 0};
    }
    return *ttl_guess_;
  }

  tsrange_t ts_range() const {
    if (!ts_guess_) {
      return {0, 0};
    }
    return *ts_guess_;
  }

  bool HasTTL() const { return ttl_guess_ != nullptr; }

  bool HasTimestamp() const { return ts_guess_ != nullptr; }

  BlobLogHeader& operator=(BlobLogHeader&& in) noexcept;
};

// Footer encapsulates the fixed information stored at the tail
// end of every blob log file.
class BlobLogFooter {
  friend class BlobFile;

 public:
  // Use this constructor when you plan to write out the footer using
  // EncodeTo(). Never use this constructor with DecodeFrom().
  BlobLogFooter();

  uint32_t magic_number() const { return magic_number_; }

  void EncodeTo(std::string* dst) const;

  Status DecodeFrom(const Slice& input);

  // convert this object to a human readable form
  std::string ToString() const;

  // footer size = 4 byte magic number
  // 8 bytes count
  // 8, 8 - ttl range
  // 8, 8 - sn range
  // 8, 8 - ts range
  // = 64
  static const size_t kFooterSize = 4 + 4 + 8 + (8 * 2) + (8 * 2) + (8 * 2);

  bool HasTTL() const { return !!ttl_range_; }

  bool HasTimestamp() const { return !!ts_range_; }

  uint64_t GetBlobCount() const { return blob_count_; }

  ttlrange_t GetTTLRange() const {
    if (ttl_range_) {
      *ttl_range_;
    }
    return {0, 0};
  }

  tsrange_t GetTimeRange() const {
    if (ts_range_) {
      return *ts_range_;
    }
    return {0, 0};
  }

  const snrange_t& GetSNRange() const { return sn_range_; }

 private:
  uint32_t magic_number_ = 0;
  uint64_t blob_count_ = 0;

  std::unique_ptr<ttlrange_t> ttl_range_;
  std::unique_ptr<tsrange_t> ts_range_;
  snrange_t sn_range_;

 private:
  void set_ttl_range(const ttlrange_t& ttl) {
    ttl_range_.reset(new ttlrange_t(ttl));
  }
  void set_time_range(const tsrange_t& ts) {
    ts_range_.reset(new tsrange_t(ts));
  }
};

extern const size_t kBlockSize;

class BlobLogRecord {
  friend class Reader;

 private:
  // this might not be set.
  uint32_t checksum_;
  uint32_t header_cksum_;
  uint32_t key_size_;
  uint64_t blob_size_;
  uint64_t time_val_;
  uint64_t ttl_val_;
  char type_;
  char subtype_;
  Slice key_;
  Slice blob_;
  std::string key_buffer_;
  std::string blob_buffer_;

 private:
  void Clear();

  char* GetKeyBuffer() { return &(key_buffer_[0]); }

  char* GetBlobBuffer() { return &(blob_buffer_[0]); }

  void ResizeKeyBuffer(size_t kbs);

  void ResizeBlobBuffer(size_t bbs);

 public:
  // Header is
  // Key Length ( 4 bytes ),
  // Blob Length ( 8 bytes),
  // ttl (8 bytes), timestamp (8 bytes),
  // type (1 byte), subtype (1 byte)
  // header checksum (4 bytes), blob checksum (4 bytes),
  // = 42
  static const size_t kHeaderSize = 4 + 4 + 8 + 8 + 4 + 8 + 1 + 1;

 public:
  BlobLogRecord();

  ~BlobLogRecord();

  const Slice& Key() const { return key_; }

  const Slice& Blob() const { return blob_; }

  uint32_t GetKeySize() const { return key_size_; }

  uint64_t GetBlobSize() const { return blob_size_; }

  bool HasTTL() const {
    return ttl_val_ != std::numeric_limits<uint32_t>::max();
  }

  uint64_t GetTTL() const { return ttl_val_; }

  uint64_t GetTimeVal() const { return time_val_; }

  char type() const { return type_; }

  char subtype() const { return subtype_; }

  uint32_t header_checksum() const { return header_cksum_; }

  uint32_t checksum() const { return checksum_; }

  Status DecodeHeaderFrom(const Slice& hdrslice);
};

}  // namespace blob_db
}  // namespace rocksdb
#endif  // ROCKSDB_LITE
