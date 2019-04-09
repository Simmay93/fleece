//
// Doc.hh
//
// Copyright © 2018 Couchbase. All rights reserved.
//

#pragma once
#include "RefCounted.hh"
#include "Value.hh"
#include "fleece/slice.hh"
#include <map>

namespace fleece { namespace impl {
    class SharedKeys;
    class Value;
    namespace internal {
        class Pointer;
    }


    class Scope {
    public:
        Scope(slice fleeceData,
              SharedKeys*,
              slice externDestination =nullslice) noexcept;
        Scope(const alloc_slice &fleeceData,
              SharedKeys*,
              slice externDestination =nullslice) noexcept;
        Scope(const Scope &parentScope,
              slice subData);
        ~Scope();

        static const Scope* containing(const Value* NONNULL) noexcept;

        slice data() const                      {return _data;}
        alloc_slice allocedData() const         {return _alloced;}

        SharedKeys* sharedKeys() const          {return _sk;}
        slice externDestination() const         {return _externDestination;}

        // For internal use:

        static SharedKeys* sharedKeys(const Value* NONNULL v) noexcept;
        const Value* resolveExternPointerTo(const void* NONNULL) const noexcept;
        static const Value* resolvePointerFrom(const internal::Pointer* NONNULL src,
                                               const void* NONNULL dst) noexcept;
        static std::pair<const Value*,slice> resolvePointerFromWithRange(
                                                                         const internal::Pointer* NONNULL src,
                                                                         const void* NONNULL dst) noexcept;
    protected:
        static const Scope* _containing(const Value* NONNULL) noexcept;
        void unregister() noexcept;

    private:
        Scope(const Scope&) =delete;
        void registr() noexcept;

        using memoryMap = std::multimap<size_t, Scope*>;
        static memoryMap *sMemoryMap;
        
        Retained<SharedKeys> _sk;                       // SharedKeys used for this Fleece data
        slice const         _externDestination;         // Extern ptr destination for this data
        slice const         _data;                      // The memory range I represent
        alloc_slice const   _alloced;                   // Retains data if it's an alloc_slice
        bool                _registered {false};        // Am I registered in sMemoryMap?
        memoryMap::iterator _iter;                      // Pointer to my entry in sMemoryMap
#if DEBUG
        uint32_t            _dataHash;                  // hash of _data, for troubleshooting
#endif
    protected:
        bool                _isDoc {false};             // True if I am a field of a Doc
    };


    /** A container for Fleece data in memory. Every Value belongs to the Doc whose memory range
        contains it. The Doc keeps track of the SharedKeys used by its Dicts, and where to resolve
        external pointers to. */
    class Doc : public RefCounted, public Scope {
    public:
        enum Trust {
            kUntrusted, kTrusted
        };

        Doc(const alloc_slice &fleeceData,
            Trust =kUntrusted,
            SharedKeys* =nullptr,
            slice externDest =nullslice) noexcept;

        Doc(const Scope &parentScope,
            slice subData,
            Trust =kUntrusted) noexcept;

        static Retained<Doc> fromFleece(const alloc_slice &fleece, Trust =kUntrusted);
        static Retained<Doc> fromJSON(slice json, SharedKeys* =nullptr);

        static RetainedConst<Doc> containing(const Value* NONNULL) noexcept;

        const Value* root() const               {return _root;}
        const Dict* asDict() const              {return _root ? _root->asDict() : nullptr;}
        const Array* asArray() const            {return _root ? _root->asArray() : nullptr;}

    protected:
        virtual ~Doc() =default;

    private:
        void init(Trust) noexcept;

        const Value*        _root {nullptr};            // The root object of the Fleece
    };

} }
