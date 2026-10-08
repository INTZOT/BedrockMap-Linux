// Copyright (c) 2017 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// libdeflate-backed Compressor for the Linux build.
//
// bedrock-level vendors third/leveldb/libdeflate_compressor.h and constructs
// leveldb::LibdeflateCompressorRaw in src/bedrock_level.cpp, but the matching
// implementation exists only in the private leveldb-mcpe build behind the
// prebuilt Windows archives in bedrock-level/libs/. The public Amulet-Team
// leveldb-mcpe repository has no libdeflate support at all, so the Linux port
// supplies this file instead of patching the submodule or the dependency.
//
// It is compiled as part of the bedrock-level target on purpose: that target
// sees the pristine leveldb headers of the library it links first and the
// vendored bedrock-level/third afterwards, which is exactly the environment
// that parses the vendored declaration. Compiling it elsewhere could pick a
// different Compressor definition and break the vtable layout.

#include "leveldb/compressor.h"
#include "leveldb/libdeflate_compressor.h"

#include <libdeflate.h>

#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

namespace leveldb {
    namespace {

        /// libdeflate keeps its match-finder tables inside the compressor object, so
        /// one instance per (raw, level) is cached per thread instead of being rebuilt
        /// for every block. The destructor releases them when the thread exits.
        class compressor_cache {
        public:
            struct entry {
                bool raw;
                int level;
                libdeflate_compressor* compressor;
            };

            ~compressor_cache() {
                for (auto& e : entries_) libdeflate_free_compressor(e.compressor);
            }

            libdeflate_compressor* get(bool raw, int level) {
                for (auto& e : entries_) {
                    if (e.raw == raw && e.level == level) return e.compressor;
                }
                libdeflate_compressor* compressor = libdeflate_alloc_compressor(level);
                entries_.push_back({raw, level, compressor});
                return compressor;
            }

        private:
            std::vector<entry> entries_;
        };

        libdeflate_compressor* compressor_for(bool raw, int level) {
            static thread_local compressor_cache cache;
            return cache.get(raw, level);
        }

        /// A decompressor keeps no state that has to be reset between streams, so a
        /// single instance per thread is enough.
        class decompressor_holder {
        public:
            decompressor_holder() : decompressor_(libdeflate_alloc_decompressor()) {}
            ~decompressor_holder() { libdeflate_free_decompressor(decompressor_); }

            [[nodiscard]] libdeflate_decompressor* get() const { return decompressor_; }

        private:
            libdeflate_decompressor* decompressor_;
        };

        libdeflate_decompressor* decompressor() {
            static thread_local decompressor_holder holder;
            return holder.get();
        }

    }  // namespace

    void LibdeflateCompressorBase::compressImpl(const char* input, size_t length, ::std::string& buffer) const {
        libdeflate_compressor* compressor = compressor_for(raw, compressionLevel);
        assert(compressor != nullptr);
        if (compressor == nullptr) return;

        const size_t bound = raw ? libdeflate_deflate_compress_bound(compressor, length)
                                 : libdeflate_zlib_compress_bound(compressor, length);
        // Same contract as ZlibCompressorBase: append to what is already buffered.
        const size_t offset = buffer.size();
        buffer.resize(offset + bound);

        const size_t written = raw ? libdeflate_deflate_compress(compressor, input, length, buffer.data() + offset, bound)
                                   : libdeflate_zlib_compress(compressor, input, length, buffer.data() + offset, bound);
        if (written == 0) {
            // compress_bound() is a worst case, so reaching this means a real error.
            assert(false);
            buffer.resize(offset);
            return;
        }
        buffer.resize(offset + written);
    }

    bool LibdeflateCompressorBase::decompress(const char* input, size_t length, ::std::string& output) const {
        libdeflate_decompressor* instance = decompressor();
        if (instance == nullptr) return false;

        // libdeflate decompresses in one shot and LevelDB does not record the
        // uncompressed size, so start from a guess and grow until it fits.
        const size_t offset = output.size();
        size_t capacity = std::max<size_t>(length * 4u, 64u * 1024u);
        for (;;) {
            output.resize(offset + capacity);
            size_t actual_in = 0;
            size_t actual_out = 0;
            const libdeflate_result result =
                raw ? libdeflate_deflate_decompress_ex(instance, input, length, output.data() + offset, capacity,
                                                       &actual_in, &actual_out)
                    : libdeflate_zlib_decompress_ex(instance, input, length, output.data() + offset, capacity,
                                                    &actual_in, &actual_out);
            if (result == LIBDEFLATE_SUCCESS) {
                output.resize(offset + actual_out);
                return true;
            }
            output.resize(offset);
            if (result != LIBDEFLATE_INSUFFICIENT_SPACE) return false;
            capacity *= 2u;
        }
    }

}  // namespace leveldb
