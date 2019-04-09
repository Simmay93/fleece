//
// Doc.cc
//
// Copyright © 2018 Couchbase. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "Doc.hh"
#include "SharedKeys.hh"
#include "Pointer.hh"
#include "JSONConverter.hh"
#include "FleeceException.hh"
#include <functional>
#include <mutex>
#include <set>
#include "betterassert.hh"

#if 0
#define Log(FMT,...) fprintf(stderr, "DOC: " # FMT "\n", __VA_ARGS__)
#else
#define Log(FMT,...)
#endif

namespace fleece { namespace impl {
    using namespace std;
    using namespace internal;

    static mutex sMutex;
    Scope::memoryMap* Scope::sMemoryMap;


    Scope::Scope(slice data, SharedKeys *sk, slice destination) noexcept
    :_sk(sk)
    ,_externDestination(destination)
    ,_data(data)
    {
        if (data)
            registr();
    }


    Scope::Scope(const alloc_slice &data, SharedKeys *sk, slice destination) noexcept
    :_sk(sk)
    ,_externDestination(destination)
    ,_data(data)
    ,_alloced(data)
    {
        if (data)
            registr();
    }


    Scope::Scope(const Scope &parentScope, slice subData)
    :_sk(parentScope.sharedKeys())
    ,_externDestination(parentScope.externDestination())
    ,_data(subData)
    ,_alloced(parentScope._alloced)
    {
        if (subData)
            assert(parentScope.data().contains(subData));
    }


    Scope::~Scope() {
        unregister();
    }


    void Scope::registr() noexcept {
        lock_guard<mutex> lock(sMutex);
        if (_usuallyFalse(!sMemoryMap))
            sMemoryMap = new multimap<size_t, Scope*>;
        auto key = size_t(_data.end());
        _iter = sMemoryMap->insert({key, this});
        _registered = true;
        Log("Register   (%p ... %p) --> Scope %p, sk=%p [Now %zu]",
            data.buf, data.end(), this, sk, sMemoryMap->size());
//#if DEBUG   // Leaving this enabled for troubleshooting
        if (_iter != sMemoryMap->begin() && prev(_iter)->first == key) {
            Scope *existing = prev(_iter)->second;
            if (existing->_data == _data && existing->_externDestination == _externDestination
                && existing->_sk == _sk) {
                Log("Duplicate  (%p ... %p) --> Scope %p, sk=%p",
                    data.buf, data.end(), this, sk);
            } else {
                FleeceException::_throw(InternalError,
                                        "Incompatible duplicate Scope %p for (%p .. %p) with sk=%p: conflicts with %p for (%p .. %p) with sk=%p",
                                        this, _data.buf, _data.end(), _sk.get(),
                                        existing, existing->_data.buf, existing->_data.end(),
                                        existing->_sk.get());
            }
        }
//#endif
#if DEBUG
        _dataHash = _data.hash();
#endif
    }


    void Scope::unregister() noexcept {
        if (_registered) {
            lock_guard<mutex> lock(sMutex);
            Log("Unregister (%p ... %p) --> Scope %p, sk=%p   [now %zu]",
                _data.buf, _data.end(), this, _sk.get(), sMemoryMap->size()-1);
            sMemoryMap->erase(_iter);
            _registered = false;
#if DEBUG
            if (_data.hash() != _dataHash)
                FleeceException::_throw(InternalError,
                    "Memory range (%p .. %p) was altered while Scope %p (sk=%p) was active. "
                    "This usually means the Scope's data was freed/invalidated before the Scope "
                    "was unregistered/deleted. Unregister it earlier!",
                                        _data.buf, _data.end(), this, _sk.get());
#endif
        }
    }


    /*static*/ const Scope* Scope::_containing(const Value *src) noexcept {
        // must have sMutex to call this
        if (_usuallyFalse(!sMemoryMap))
            return nullptr;
        auto i = sMemoryMap->upper_bound(size_t(src));
        if (_usuallyFalse(i == sMemoryMap->end()))
            return nullptr;
        Scope *scope = i->second;
        if (_usuallyFalse(src < scope->_data.buf))
            return nullptr;
        return scope;
    }


    /*static*/ const Scope* Scope::containing(const Value *src) noexcept {
        lock_guard<mutex> lock(sMutex);
        return _containing(src);
    }


    /*static*/ SharedKeys* Scope::sharedKeys(const Value *v) noexcept {
        lock_guard<mutex> lock(sMutex);
        auto scope = _containing(v);
        return scope ? scope->sharedKeys() : nullptr;
    }


    const Value* Scope::resolveExternPointerTo(const void* dst) const noexcept {
        dst = offsetby(dst, (char*)_externDestination.end() - (char*)_data.buf);
        if (_usuallyFalse(!_externDestination.contains(dst)))
            return nullptr;
        return (const Value*)dst;
    }


    /*static*/ const Value* Scope::resolvePointerFrom(const internal::Pointer* src,
                                                      const void *dst) noexcept
    {
        lock_guard<mutex> lock(sMutex);
        auto scope = _containing((const Value*)src);
        return scope ? scope->resolveExternPointerTo(dst) : nullptr;
    }


    /*static*/ pair<const Value*,slice> Scope::resolvePointerFromWithRange(const Pointer* src,
                                                                         const void* dst) noexcept
    {
        lock_guard<mutex> lock(sMutex);
        auto scope = _containing((const Value*)src);
        if (!scope)
            return { };
        return {scope->resolveExternPointerTo(dst), scope->externDestination()};
    }


#pragma mark - DOC:


    Doc::Doc(const alloc_slice &data, Trust trust, SharedKeys *sk, slice destination) noexcept
    :Scope(data, sk, destination)
    {
        init(trust);
    }


    Doc::Doc(const Scope &parentScope,
             slice subData,
             Trust trust) noexcept
    :Scope(parentScope, subData)
    {
        init(trust);
    }


    void Doc::init(Trust trust) noexcept {
        if (data()) {
            _root = trust ? Value::fromTrustedData(data()) : Value::fromData(data());
            if (!_root)
                unregister();
        }
        _isDoc = true;
    }


    Retained<Doc> Doc::fromFleece(const alloc_slice &fleece, Trust trust) {
        return new Doc(fleece, trust);
    }

    Retained<Doc> Doc::fromJSON(slice json, SharedKeys *sk) {
        return new Doc(JSONConverter::convertJSON(json, sk), kTrusted, sk);
    }


    /*static*/ RetainedConst<Doc> Doc::containing(const Value *src) noexcept {
        lock_guard<mutex> lock(sMutex);
        Doc *scope = (Doc*) _containing(src);
        if (!scope)
            return nullptr;
        assert(scope->_isDoc);
        return RetainedConst<Doc>(scope);
    }

} }
