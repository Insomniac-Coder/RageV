#pragma once

// The entity-component store, replacing EnTT (roadmap 10.2).
//
// **Bare bones on purpose.** EnTT is thirty thousand lines and this engine used
// twelve registry calls and four view operations of it: create, destroy, valid,
// clear, emplace, get, try_get, remove, all_of, view, each and a destruction
// signal. Everything here exists because one of those needed it, and nothing
// here exists because an ECS usually has it -- no groups, no owning views, no
// reflection, no snapshot, no runtime-typed storage, no allocator policy.
//
// The shape is the standard one and there is no cleverness in it: a sparse set
// per component type, a dense array of entities beside a parallel array of
// components, and an index from entity to position. Iteration walks the dense
// array, so it is sequential over the components themselves.
//
// **What the engine needs from it, in order of how much it matters:**
//
//   1. `Get<T>` is called per entity in the transform walk, which runs several
//      times a frame over every entity in the scene. It is two loads and an
//      index, and it must stay that way -- see ComponentTypeIndex.
//   2. A view resolves its pools *once*, at construction, so iterating one
//      never looks a type up. This is the difference between the draw list
//      costing what it costs and costing twice that.
//   3. Iteration order is the order components were added, with removals
//      swapping the last element into the hole. The draw list depends on
//      *an* order rather than a particular one, but it depends on it being
//      the same from frame to frame on a scene nobody touched.

#include "RageV/Core/Log.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace RageV::ECS
{
	// ---------------------------------------------------------------------
	// Handles
	// ---------------------------------------------------------------------

	// Twenty bits of index and twelve of version, in one word.
	//
	// The version is what makes a stale handle detectable: destroying an entity
	// bumps it, so a handle kept across the destruction refers to an index that
	// has moved on and `Valid` says no. Without it a recycled index would make
	// an old handle silently address a new entity, which is the failure that
	// looks like data corruption rather than like a dangling reference.
	//
	// Twelve bits is 4096 destroy-and-recreate cycles before a version repeats
	// on one index, and twenty is a million live entities. Both are far past
	// what this engine has been run with -- the largest scale fixture is a
	// hundred and twenty thousand.
	enum class Entity : uint32_t {};

	inline constexpr uint32_t kIndexBits = 20;
	inline constexpr uint32_t kIndexMask = (1u << kIndexBits) - 1;
	inline constexpr uint32_t kMaxEntities = kIndexMask;   // one reserved for Null

	// All ones: an index of 0xFFFFF, which `Create` never hands out.
	inline constexpr Entity Null = Entity{ 0xFFFFFFFFu };

	inline constexpr uint32_t IndexOf(Entity e) { return (uint32_t)e & kIndexMask; }
	inline constexpr uint32_t VersionOf(Entity e) { return (uint32_t)e >> kIndexBits; }
	inline constexpr Entity MakeEntity(uint32_t index, uint32_t version)
	{
		return Entity{ (version << kIndexBits) | (index & kIndexMask) };
	}

	// ---------------------------------------------------------------------
	// Type identity that survives a DLL boundary
	// ---------------------------------------------------------------------

	// A hash of the compiler's own name for T.
	//
	// **Not a counter.** A counter incremented by a function-local static would
	// give the engine DLL and the editor executable different numbers for the
	// same component, because a static inside an inline template is per module
	// -- the trap this codebase has already been caught by once, with ImGui's
	// per-module state. Two modules would then reach two different pools for
	// TransformComponent and nothing would say so.
	//
	// This produces the same value in every module because it is computed from
	// the same string by the same compiler.
	template<typename T>
	constexpr uint64_t TypeHash()
	{
#if defined(_MSC_VER)
		constexpr std::string_view name = __FUNCSIG__;
#else
		constexpr std::string_view name = __PRETTY_FUNCTION__;
#endif
		uint64_t hash = 14695981039346656037ull;      // FNV-1a offset basis
		for (char c : name)
		{
			hash ^= (uint64_t)(unsigned char)c;
			hash *= 1099511628211ull;                 // FNV-1a prime
		}
		return hash;
	}

	// The dense index this component type's pool lives at, one shared numbering
	// for the whole process.
	//
	// Defined in the engine library and exported, so every module asking about
	// the same type gets the same answer however many modules there are. The
	// static below then caches it per module, which is what keeps `Get` down to
	// a load rather than a hash lookup.
	uint32_t RegisterComponentType(uint64_t hash);

	template<typename T>
	inline uint32_t ComponentTypeIndex()
	{
		static const uint32_t index = RegisterComponentType(TypeHash<T>());
		return index;
	}

	class Registry;

	// ---------------------------------------------------------------------
	// Pools
	// ---------------------------------------------------------------------

	// What the registry can do to a pool without knowing what it holds: ask
	// whether an entity is in it, take the entity out, and empty it. Destroying
	// an entity and clearing the registry both need exactly this and nothing
	// more, which is the whole reason the base class exists.
	class PoolBase
	{
	public:
		virtual ~PoolBase() = default;
		virtual bool Contains(Entity entity) const = 0;
		virtual void Erase(Registry& registry, Entity entity) = 0;
		virtual void Clear(Registry& registry) = 0;
	};

	template<typename T>
	class Pool final : public PoolBase
	{
	public:
		static constexpr uint32_t kAbsent = 0xFFFFFFFFu;

		// **Components live in fixed-size pages, and that is not a detail.**
		//
		// A contiguous vector would be simpler and marginally faster to walk,
		// and it would move every component whenever it grew -- which breaks
		// every reference anyone is holding. The engine holds them constantly:
		// `AddComponent` hands one back and callers configure it over the next
		// several lines, the draw list borrows a `TransformComponent*` for the
		// length of a frame, and the transform walk binds `auto&` while it
		// recurses. Adding any component to any entity would invalidate all of
		// it, silently, and only sometimes.
		//
		// So insertion never moves what is already there. A page is allocated
		// when the one before it fills and nothing is ever reallocated.
		// Removal still moves one element -- the last, into the hole -- which
		// is the single documented way a reference goes stale, and the same
		// rule the library this replaced had.
		static constexpr uint32_t kPageShift = 10;
		static constexpr uint32_t kPageSize = 1u << kPageShift;   // 1024
		static constexpr uint32_t kPageMask = kPageSize - 1;

		bool Contains(Entity entity) const override
		{
			const uint32_t index = IndexOf(entity);
			return index < m_Sparse.size() && m_Sparse[index] != kAbsent;
		}

		// The component, or null. A bounds check, two loads and an index --
		// which is the budget the transform walk was written against.
		T* TryGet(Entity entity)
		{
			const uint32_t index = IndexOf(entity);
			if (index >= m_Sparse.size())
				return nullptr;
			const uint32_t slot = m_Sparse[index];
			return slot == kAbsent ? nullptr : &Element(slot);
		}

		const T* TryGet(Entity entity) const
		{
			return const_cast<Pool*>(this)->TryGet(entity);
		}

		// For a caller that has *already* established the entity is in this
		// pool -- which a view has, by construction, for every entity it hands
		// out. No branches.
		T& GetUnchecked(Entity entity) { return Element(m_Sparse[IndexOf(entity)]); }

		template<typename... Args>
		T& Emplace(Entity entity, Args&&... args)
		{
			const uint32_t index = IndexOf(entity);
			if (index >= m_Sparse.size())
				m_Sparse.resize((size_t)index + 1, kAbsent);

			// Already present: overwritten rather than duplicated, because the
			// alternative is two components of one type on one entity and no
			// way to say which is meant.
			if (m_Sparse[index] != kAbsent)
			{
				T& existing = Element(m_Sparse[index]);
				existing = T(std::forward<Args>(args)...);
				return existing;
			}

			const uint32_t slot = (uint32_t)m_Dense.size();
			Reserve(slot);
			Element(slot) = T(std::forward<Args>(args)...);

			m_Sparse[index] = slot;
			m_Dense.push_back(entity);
			return Element(slot);
		}

		// **Swap and pop**, which is what makes removal constant time and the
		// one place a reference into this pool can go stale: the component that
		// was last moves into the hole. Iteration order changes with it.
		// Nothing in the engine depends on the order surviving a removal;
		// several things depend on it surviving a frame in which nothing was
		// removed.
		void Erase(Registry& registry, Entity entity) override
		{
			const uint32_t index = IndexOf(entity);
			if (index >= m_Sparse.size())
				return;

			const uint32_t slot = m_Sparse[index];
			if (slot == kAbsent)
				return;

			Notify(registry, entity);

			// The listener may have taken it out already -- the script handler
			// destroys the entity it is told about, which removes this on the
			// way -- so nothing below may assume the slot is still there.
			if (index >= m_Sparse.size() || m_Sparse[index] != slot)
				return;

			const uint32_t last = (uint32_t)m_Dense.size() - 1;
			if (slot != last)
			{
				m_Dense[slot] = m_Dense[last];
				Element(slot) = std::move(Element(last));
				m_Sparse[IndexOf(m_Dense[slot])] = slot;
			}

			// Left empty rather than left alone: a component holding a string
			// or a vector would otherwise keep it until something reused the
			// slot, which on a scene that removes more than it adds is a leak
			// in everything but name.
			Element(last) = T{};

			m_Dense.pop_back();
			m_Sparse[index] = kAbsent;
		}

		void Clear(Registry& registry) override
		{
			while (!m_Dense.empty())
			{
				const Entity entity = m_Dense.back();
				Notify(registry, entity);

				// As in Erase: the listener may already have removed it, and
				// asking again would find nothing, remove nothing, and leave
				// this loop on the same entity forever.
				if (m_Dense.empty() || m_Dense.back() != entity)
					continue;

				Element((uint32_t)m_Dense.size() - 1) = T{};
				m_Sparse[IndexOf(entity)] = kAbsent;
				m_Dense.pop_back();
			}

			m_Sparse.clear();
			m_Pages.clear();
		}

		size_t Size() const { return m_Dense.size(); }
		const std::vector<Entity>& Entities() const { return m_Dense; }

		// One listener per pool, which is all three call sites need. A vector
		// would be the general answer and there is nothing general here.
		using Listener = void (*)(void*, Registry&, Entity);
		void Listen(Listener listener, void* context)
		{
			m_Listener = listener;
			m_Context = context;
		}

	private:
		T& Element(uint32_t slot) { return m_Pages[slot >> kPageShift][slot & kPageMask]; }

		void Reserve(uint32_t slot)
		{
			const uint32_t page = slot >> kPageShift;
			while (page >= m_Pages.size())
				m_Pages.push_back(std::make_unique<T[]>(kPageSize));
		}

		void Notify(Registry& registry, Entity entity)
		{
			if (m_Listener)
				m_Listener(m_Context, registry, entity);
		}

		std::vector<Entity> m_Dense;
		// Parallel to m_Dense by slot, in pages that never move.
		std::vector<std::unique_ptr<T[]>> m_Pages;
		// Indexed by an entity's *index*, holding its position in the dense
		// array. Flat rather than paged: at this engine's entity counts a flat
		// array of four-byte slots is smaller than the indirection would cost,
		// and nothing holds a reference into it.
		std::vector<uint32_t> m_Sparse;

		Listener m_Listener = nullptr;
		void*    m_Context = nullptr;
	};


	// ---------------------------------------------------------------------
	// Views
	// ---------------------------------------------------------------------

	// A view over the entities that have all of `Components`.
	//
	// **The pools are resolved once, here**, and every iteration and every
	// `Get` afterwards is a direct access. Resolving per element instead is the
	// difference between the draw list's walk costing what it does and costing
	// roughly twice that, and it is the only performance decision in this file
	// that had to be made deliberately.
	template<typename... Components>
	class View
	{
		static_assert(sizeof...(Components) > 0, "A view needs at least one component");

	public:
		explicit View(std::tuple<Pool<std::remove_const_t<Components>>*...> pools)
			: m_Pools(pools)
		{
			// The smallest pool decides the walk: every entity in the result
			// must be in all of them, so starting anywhere else is extra
			// entities to reject.
			//
			// Its *identity* is kept as well as its entities, because the
			// match below then skips it -- every entity it hands out is in it
			// by definition, and on a two-component view checking it anyway is
			// half the work of matching.
			size_t smallest = (size_t)-1;
			bool empty = false;

			std::apply([&](auto*... pool)
			{
				([&]
				{
					if (!pool)
					{
						empty = true;
						return;
					}
					if (pool->Size() < smallest)
					{
						smallest = pool->Size();
						m_LeadEntities = &pool->Entities();
						m_Lead = (const void*)pool;
					}
				}(), ...);
			}, m_Pools);

			if (empty)
			{
				m_LeadEntities = nullptr;
				m_Lead = nullptr;
			}
		}

		// Iterates the lead pool and skips what is not in the others. A plain
		// forward iterator: nothing here needs more.
		class Iterator
		{
		public:
			Iterator(const View* view, const std::vector<Entity>* entities,
					 size_t at, size_t end)
				: m_View(view), m_Entities(entities), m_At(at), m_End(end)
			{
				Advance();
			}

			// A reference into the pool's dense array rather than a copy, so a
			// caller writing `for (auto& entity : view)` binds to something
			// that exists. It is const because the dense array's order is the
			// pool's business.
			const Entity& operator*() const { return (*m_Entities)[m_At]; }
			Iterator& operator++() { m_At++; Advance(); return *this; }
			bool operator!=(const Iterator& other) const { return m_At != other.m_At; }
			bool operator==(const Iterator& other) const { return m_At == other.m_At; }

		private:
			// **The vector rather than its data pointer, and an end fixed when
			// the loop started.** Both halves matter and each was wrong on its
			// own once:
			//
			//   - caching `data()` dangles, because a script running inside a
			//     loop over its own component type can add one and reallocate
			//     the dense array. Scripts do that during a fixed step, so it
			//     is what the engine does the frame a button spawns something;
			//   - re-reading the size each step *hangs*. A range-for evaluates
			//     `end()` once, so if the pool grows the cursor walks past the
			//     index that loop is comparing against and the two are never
			//     equal again. That is an infinite loop inside a fixed step,
			//     with audio still playing, which is exactly how it presented.
			//
			// So: index through the vector, and stop where the loop was told
			// to stop. A one-component view still needs no match test at all,
			// which is the part of the optimisation that survives.
			void Advance()
			{
				if constexpr (sizeof...(Components) == 1)
					return;
				else
				{
					while (m_At < m_End && !m_View->Matches((*m_Entities)[m_At]))
						m_At++;
				}
			}

			const View*                m_View;
			const std::vector<Entity>* m_Entities;
			size_t                     m_At;
			size_t                     m_End;
		};

		Iterator MakeIterator(size_t at) const
		{
			const size_t end = m_LeadEntities ? m_LeadEntities->size() : 0;
			return Iterator(this, m_LeadEntities, at, end);
		}

		Iterator begin() const { return MakeIterator(0); }
		Iterator end() const
		{
			return MakeIterator(m_LeadEntities ? m_LeadEntities->size() : 0);
		}

		// An upper bound, not a count: the lead pool's size, before the other
		// pools reject anything. Callers use it to reserve.
		size_t SizeHint() const { return m_LeadEntities ? m_LeadEntities->size() : 0; }

		// The real count, which for a one-component view is the same thing and
		// for any other means walking.
		size_t Size() const
		{
			if constexpr (sizeof...(Components) == 1)
				return SizeHint();
			else
			{
				size_t n = 0;
				for (auto it = begin(); it != end(); ++it)
					n++;
				return n;
			}
		}

		// Every match, with its components. The callback takes the entity and
		// then one reference per component, in the order the view names them.
		template<typename F>
		void Each(F&& fn) const
		{
			for (Entity entity : *this)
				fn(entity, Get<Components>(entity)...);
		}

		// One component, by reference.
		//
		// Unchecked: a view only ever hands out entities that are in every one
		// of its pools, so the presence test has already been made and making
		// it again is a branch per component per entity.
		template<typename T>
		T& Get(Entity entity) const
		{
			auto* pool = std::get<Pool<std::remove_const_t<T>>*>(m_Pools);
			return pool->GetUnchecked(entity);
		}

		// Several, as a tuple, so `auto [a, b] = view.Get<A, B>(e)` binds.
		template<typename A, typename B, typename... Rest>
		std::tuple<A&, B&, Rest&...> Get(Entity entity) const
		{
			return std::tie(Get<A>(entity), Get<B>(entity), Get<Rest>(entity)...);
		}

	private:
		bool Matches(Entity entity) const
		{
			bool all = true;
			std::apply([&](auto*... pool)
			{
				// The lead pool is where this entity came from.
				((all = all && ((const void*)pool == m_Lead || pool->Contains(entity))), ...);
			}, m_Pools);
			return all;
		}

		std::tuple<Pool<std::remove_const_t<Components>>*...> m_Pools;
		const std::vector<Entity>* m_LeadEntities = nullptr;
		const void* m_Lead = nullptr;
	};

	// ---------------------------------------------------------------------
	// Registry
	// ---------------------------------------------------------------------

	class Registry
	{
	public:
		Registry() = default;
		Registry(const Registry&) = delete;
		Registry& operator=(const Registry&) = delete;
		~Registry() { Clear(); }

		Entity Create()
		{
			if (!m_Free.empty())
			{
				const uint32_t index = m_Free.back();
				m_Free.pop_back();
				m_Alive[index] = true;
				return MakeEntity(index, m_Versions[index]);
			}

			RV_CORE_ASSERT(m_Versions.size() < kMaxEntities,
						   "ECS: out of entity indices (the handle is 20 bits of index)");

			const uint32_t index = (uint32_t)m_Versions.size();
			m_Versions.push_back(0);
			m_Alive.push_back(true);
			return MakeEntity(index, 0);
		}

		// **The version alone answers this**, and not because reading the alive
		// flag as well would be wrong -- because it is redundant. Destroying an
		// entity bumps its version, and no handle carrying the new version
		// exists until Create hands one out. So a handle whose version matches
		// is a handle to a live entity, and one whose version does not is stale
		// whether the index is free or reused.
		bool Valid(Entity entity) const
		{
			const uint32_t index = IndexOf(entity);
			return index < m_Versions.size() && m_Versions[index] == VersionOf(entity);
		}

		void Destroy(Entity entity)
		{
			if (!Valid(entity))
				return;

			// Every pool, because a component type nothing asked about still
			// has a destructor and may still have a listener.
			for (auto& pool : m_Pools)
				if (pool)
					pool->Erase(*this, entity);

			const uint32_t index = IndexOf(entity);
			m_Versions[index] = (m_Versions[index] + 1) & (0xFFFFFFFFu >> kIndexBits);
			m_Alive[index] = false;
			m_Free.push_back(index);
		}

		// Empties every pool and every entity. Pools first, so destruction
		// listeners run against a registry whose entities are all still valid
		// -- the script handlers ask the scene about the entity they are being
		// told about, and a half-torn-down registry would answer wrongly.
		void Clear()
		{
			for (auto& pool : m_Pools)
				if (pool)
					pool->Clear(*this);

			m_Versions.clear();
			m_Alive.clear();
			m_Free.clear();
		}

		template<typename T, typename... Args>
		T& Emplace(Entity entity, Args&&... args)
		{
			return PoolFor<T>().Emplace(entity, std::forward<Args>(args)...);
		}

		// **FindPool, not PoolFor**: reading must not allocate. Asking for a
		// component an entity does not have is a caller error either way, and
		// creating an empty pool on the way to saying so would leave the
		// registry a little larger every time something asked a question.
		template<typename T>
		T& Get(Entity entity)
		{
			Pool<T>* pool = FindPool<T>();
			RV_CORE_ASSERT(pool, "ECS: Get for a component type no entity has");
			T* component = pool->TryGet(entity);
			RV_CORE_ASSERT(component, "ECS: Get on an entity that has no such component");
			return *component;
		}

		template<typename T>
		const T& Get(Entity entity) const
		{
			return const_cast<Registry*>(this)->Get<T>(entity);
		}

		template<typename T>
		T* TryGet(Entity entity)
		{
			Pool<T>* pool = FindPool<T>();
			return pool ? pool->TryGet(entity) : nullptr;
		}

		template<typename T>
		const T* TryGet(Entity entity) const
		{
			return const_cast<Registry*>(this)->TryGet<T>(entity);
		}

		template<typename T>
		void Remove(Entity entity)
		{
			if (Pool<T>* pool = FindPool<T>())
				pool->Erase(*this, entity);
		}

		template<typename... T>
		bool AllOf(Entity entity) const
		{
			return (HasOne<T>(entity) && ...);
		}

		template<typename... T>
		View<T...> GetView()
		{
			return View<T...>(std::make_tuple(FindPool<std::remove_const_t<T>>()...));
		}

		// The same from a const registry, which several of the scene's query
		// methods are. The cast is safe for the reason the overload exists: a
		// view reads, and one asked for `const T` hands back `const T&`. It is
		// the constness of the *components* that a caller cares about, and that
		// travels in the view's own template arguments.
		template<typename... T>
		View<T...> GetView() const
		{
			return const_cast<Registry*>(this)->GetView<T...>();
		}

		// Every live entity. One caller, and it wants the handle only.
		template<typename F>
		void Each(F&& fn) const
		{
			for (uint32_t index = 0; index < (uint32_t)m_Versions.size(); index++)
				if (m_Alive[index])
					fn(MakeEntity(index, m_Versions[index]));
		}

		// Called just before a component of this type leaves an entity, by
		// removal, by the entity's destruction, or by the registry being
		// cleared. One listener per type -- a signal that cannot be forgotten
		// at a call site is the whole point, and three types use it.
		template<typename T, typename C>
		void OnDestroy(void (C::*method)(Registry&, Entity), C* instance)
		{
			struct Binding
			{
				void (C::*Method)(Registry&, Entity);
				C* Instance;
			};

			struct Trampoline
			{
				static void Call(void* context, Registry& registry, Entity entity)
				{
					auto* bound = static_cast<Binding*>(context);
					(bound->Instance->*bound->Method)(registry, entity);
				}
			};

			// Owned by the registry, because the pool only holds a pointer.
			auto binding = std::make_unique<Binding>();
			binding->Method = method;
			binding->Instance = instance;

			PoolFor<T>().Listen(&Trampoline::Call, binding.get());
			m_Bindings.push_back(std::unique_ptr<void, void(*)(void*)>(
				binding.release(), [](void* p) { delete static_cast<Binding*>(p); }));
		}

	private:
		template<typename T>
		bool HasOne(Entity entity) const
		{
			const Pool<T>* pool = const_cast<Registry*>(this)->FindPool<T>();
			return pool && pool->Contains(entity);
		}

		// The pool if it exists, without making one. Everything that only
		// *reads* goes through here, so asking about a component type no scene
		// has ever used costs nothing and allocates nothing.
		template<typename T>
		Pool<T>* FindPool()
		{
			const uint32_t index = ComponentTypeIndex<T>();
			if (index >= m_Pools.size() || !m_Pools[index])
				return nullptr;
			return static_cast<Pool<T>*>(m_Pools[index].get());
		}

		template<typename T>
		Pool<T>& PoolFor()
		{
			const uint32_t index = ComponentTypeIndex<T>();
			if (index >= m_Pools.size())
				m_Pools.resize((size_t)index + 1);
			if (!m_Pools[index])
				m_Pools[index] = std::make_unique<Pool<T>>();
			return *static_cast<Pool<T>*>(m_Pools[index].get());
		}

		std::vector<uint32_t> m_Versions;
		// Read by `Each` and by nothing else -- `Valid` gets the same answer
		// from the version. A byte rather than `std::vector<bool>`, whose bit
		// packing costs a shift and a mask on every access to save seven bits
		// per entity.
		std::vector<uint8_t>  m_Alive;
		std::vector<uint32_t> m_Free;
		std::vector<std::unique_ptr<PoolBase>> m_Pools;
		std::vector<std::unique_ptr<void, void(*)(void*)>> m_Bindings;
	};
}
