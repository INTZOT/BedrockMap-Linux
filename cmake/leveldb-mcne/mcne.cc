// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 HTMonkeyG
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ----------------------------------------------------------------------------
//
// Implementation of the NetEase ("mcne") XOR file codec for the Linux build.
//
// bedrock-level vendors leveldb/mcne.h, which comes from
// https://github.com/HTMonkeyG/leveldb-mcne, but the matching implementation
// only exists in the private leveldb-mcpe build behind the prebuilt archives in
// bedrock-level/libs/. The public Amulet-Team repository has neither.
//
// This file is compiled as part of the bedrock-level target so it sees exactly
// the headers that parse the vendored declaration; the XOR scheme itself is
// ported from the upstream MIT implementation.

#include "leveldb/mcne.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>

namespace leveldb {
    namespace {
        union EncHeader {
            char buf[McneWrapper::kMagicNumSize];
            int num;
        };

        /// Circular XOR of data->size() bytes, starting at the given stream offset.
        void xorInPlace(char* scratch, const Slice& data, const std::string& key, size_t offset) {
            if (key.empty()) return;
            offset %= key.size();
            for (size_t i = 0, j = offset; i < data.size(); ++i, ++j) {
                if (j >= key.size()) j %= key.size();
                scratch[i] ^= key[j];
            }
        }

        /// The mcne format deliberately leaves the log, LOCK and sidecar files in
        /// plain form; only the table and manifest data is obfuscated.
        bool mayEncrypt(const std::string& fname) {
            if (fname.length() < 4) return true;
            if (fname.find(".log") != std::string::npos) return false;
            if (fname.find("LOCK") != std::string::npos) return false;
            if (fname.find(".json") != std::string::npos) return false;
            if (fname.find(".bak") != std::string::npos) return false;
            return true;
        }

        /// Key bytes for the wrapping classes; the header copies them, so the
        /// Slice only has to stay valid for the length of the constructor call.
        Slice keySlice(const std::string& key) { return Slice(key); }
    }  // namespace

    // - McneSequentialFile ---------------------------------------------------

    McneSequentialFile::McneSequentialFile(SequentialFile* file, const Slice& xorKey)
        : pFile(file), key(xorKey.data(), xorKey.size()), offset(0), isEncrypted(!key.empty()) {
        assert(file != nullptr);
    }

    Status McneSequentialFile::Read(size_t n, Slice* result, char* scratch) {
        const Status s = pFile->Read(n, result, scratch);
        if (s.ok() && isEncrypted) {
            // The magic number was consumed by the wrapper before this object was created.
            xorInPlace(scratch, *result, key, offset);
            offset += result->size();
        }
        return s;
    }

    Status McneSequentialFile::Skip(uint64_t n) {
        const Status s = pFile->Skip(n);
        if (s.ok()) offset += static_cast<size_t>(n);
        return s;
    }

    // - McneRandomAccessFile -------------------------------------------------

    McneRandomAccessFile::McneRandomAccessFile(RandomAccessFile* file, const Slice& xorKey)
        : pFile(file), key(xorKey.data(), xorKey.size()), isEncrypted(!key.empty()) {
        assert(file != nullptr);
    }

    Status McneRandomAccessFile::Read(uint64_t offset, size_t n, Slice* result, char* scratch) const {
        // Reads are stream offsets; skip the magic number in the underlying file.
        const Status s = pFile->Read(McneWrapper::kMagicNumSize + offset, n, result, scratch);
        if (s.ok() && isEncrypted) xorInPlace(scratch, *result, key, static_cast<size_t>(offset));
        return s;
    }

    // - McneWritableFile -----------------------------------------------------

    McneWritableFile::McneWritableFile(WritableFile* file, const Slice& xorKey)
        : pFile(file), key(xorKey.data(), xorKey.size()), offset(0), isEncrypted(!key.empty()) {
        assert(file != nullptr);
    }

    Status McneWritableFile::Append(const Slice& data) {
        if (!isEncrypted) return pFile->Append(data);
        buffer.assign(data.data(), data.size());
        const Slice encoded(buffer);
        xorInPlace(buffer.data(), encoded, key, offset);
        const Status s = pFile->Append(encoded);
        if (s.ok()) offset += data.size();
        return s;
    }

    // - McneWrapper ----------------------------------------------------------

    McneWrapper::McneWrapper(Env* pEnv, const Slice& xorKey) : EnvWrapper(pEnv), key(xorKey.data(), xorKey.size()) {
        assert(pEnv != nullptr);
    }

    McneWrapper::McneWrapper(const Slice& xorKey) : EnvWrapper(Env::Default()), key(xorKey.data(), xorKey.size()) {}

    Status McneWrapper::NewSequentialFile(const std::string& fname, SequentialFile** result) {
        SequentialFile* file = nullptr;
        const Status s = target()->NewSequentialFile(fname, &file);
        if (!s.ok()) return s;

        if (key.empty() || !mayEncrypt(fname)) {
            *result = file;
            return Status::OK();
        }

        EncHeader header{};
        Slice magic;
        const Status read = file->Read(kMagicNumSize, &magic, header.buf);
        if (!read.ok()) {
            delete file;
            return read;
        }
        if (header.num != kMagicNum) {
            delete file;
            return Status::Corruption("mcne: magic number mismatch", fname);
        }

        *result = new McneSequentialFile(file, keySlice(key));
        return Status::OK();
    }

    Status McneWrapper::NewRandomAccessFile(const std::string& fname, RandomAccessFile** result) {
        RandomAccessFile* file = nullptr;
        const Status s = target()->NewRandomAccessFile(fname, &file);
        if (!s.ok()) return s;

        if (key.empty() || !mayEncrypt(fname)) {
            *result = file;
            return Status::OK();
        }

        EncHeader header{};
        Slice magic;
        const Status read = file->Read(0, kMagicNumSize, &magic, header.buf);
        if (!read.ok()) {
            delete file;
            return read;
        }
        if (header.num != kMagicNum) {
            delete file;
            return Status::Corruption("mcne: magic number mismatch", fname);
        }

        *result = new McneRandomAccessFile(file, keySlice(key));
        return Status::OK();
    }

    Status McneWrapper::NewWritableFile(const std::string& fname, WritableFile** result) {
        WritableFile* file = nullptr;
        const Status s = target()->NewWritableFile(fname, &file);
        if (!s.ok()) return s;

        if (key.empty() || !mayEncrypt(fname)) {
            *result = file;
            return Status::OK();
        }

        EncHeader header{};
        header.num = kMagicNum;
        const Slice magic(header.buf, kMagicNumSize);
        const Status write = file->Append(magic);
        if (!write.ok()) {
            delete file;
            return write;
        }

        *result = new McneWritableFile(file, keySlice(key));
        return Status::OK();
    }

    Status McneWrapper::NewAppendableFile(const std::string& fname, WritableFile** result) {
        // The stream offset of an existing file cannot be recovered reliably, so
        // appending is rejected rather than writing a misaligned stream.
        (void)result;
        return Status::NotSupported("mcne: appendable files are not supported", fname);
    }

}  // namespace leveldb
