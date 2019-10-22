#pragma once
/*
 * Copyright (c) 2019 Contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <graphene/chain/database.hpp>

namespace graphene { namespace chain {

namespace impl {
template<typename...> using make_void = void;
namespace TL = fc::typelist;

struct cow_refletion_data {
   const database& db;
   object_id_type object_id;
   std::unique_ptr<object> written;
   std::function<void(cow_refletion_data&, database&)> update;

   template<typename T>
   static cow_refletion_data create(const database& db, object_id_type object_id) {
      cow_refletion_data data(db, object_id);
      data.update = [](cow_refletion_data& data, database& mutable_db) {
         if (data.written == nullptr)
            return;
         const T& dest = mutable_db.get<T>(data.object_id);
         T* src = dynamic_cast<T*>(data.written.get());
         FC_ASSERT(src != nullptr, "LOGIC ERROR: Tried to update object with incorrect source type. "
                                   "Please report this error.");
         mutable_db.modify(dest, [src](T& dest) { dest = std::move(*src); });
         data.written.reset();
      };
      return data;
   }
   template<typename T>
   T& get_written() {
      FC_ASSERT(written, "LOGIC ERROR: Tried to fetch written object when none exists. Please report this error.");
      T* ptr = dynamic_cast<T*>(&*written);
      FC_ASSERT(ptr != nullptr, "LOGIC ERROR: Tried to fetch written object with incorrect type. "
                                "Please report this error.");
      return *ptr;
   }

private:
   cow_refletion_data(const database& db, object_id_type object_id) : db(db), object_id(object_id) {}
};
struct cow_data_lt {
   using is_transparent=void;
   bool operator()(const cow_refletion_data& a, const cow_refletion_data& b) const {
      return a.object_id < b.object_id;
   }
   bool operator()(const cow_refletion_data& a, const object_id_type& b) const { return a.object_id < b; }
   bool operator()(const object_id_type& a, const cow_refletion_data& b) const { return a < b.object_id; }
};

// Flatten a list of reflectors into a single getter
// Base case: list of 1; return its getter
template<typename Reflectors, typename Root = typename TL::first<Reflectors>::container,
         typename Field = typename TL::last<Reflectors>::type,
         std::enable_if_t<TL::length<Reflectors>() == 1, bool> = true>
auto make_getter() {
   return [](Root& o) -> Field& { return TL::first<Reflectors>::get(o); };
}
// Recursive case: compute the getter for the list except the first reflector, and wrap it in the getter from the
// first reflector
template<typename Reflectors, typename Root = typename TL::first<Reflectors>::container,
         typename Field = typename TL::last<Reflectors>::type,
         std::enable_if_t<TL::length<Reflectors>() != 1, bool> = true>
auto make_getter() {
   return [inner = make_getter<TL::slice<Reflectors, 1>>()](Root& root) -> Field& {
      return inner(TL::first<Reflectors>::get(root));
   };
}

template<typename Reflectors, typename Data, bool is_const>
struct cow_field_reflection {
   static_assert(std::is_same<Data, cow_refletion_data>::value, "Unexpected type for reflection data");
   static_assert(is_const, "COW reflections are for const types only");
   using RootObject = typename TL::first<Reflectors>::container;
   using Field = typename TL::last<Reflectors>::type;
   const Field& field;
   cow_refletion_data* data;
   cow_field_reflection(const Field& field, Data* data) : field(field), data(data) {}

   template<typename T>
   using cow_object = fc::object_reflection<const T, cow_refletion_data, cow_field_reflection, Reflectors>;

   std::function<Field&(RootObject&)> make_getter() const {
      static std::function<Field&(RootObject&)> cache;
      if (cache) return cache;
      return cache = impl::make_getter<Reflectors>();
   }

   /// Get a const reference to the field
   const Field& get() const {
      if (data->written)
         return make_getter()(data->get_written<RootObject>());
      return field;
   }
   /// Get a mutable reference to the field (triggers a copy)
   Field& set() {
      if (!data->written)
         data->written = data->db.get_object(data->object_id).clone();
      return make_getter()(data->get_written<RootObject>());
   }

   /// Function call operator, returns a Field (for types without reflections only)
   template<typename F = Field, std::enable_if_t<!fc::object_reflection<F>::is_defined, bool> = true>
   const Field& operator()() const { return get(); }
   /// Function call operator, returns a cow_object<Field> (for types with reflections only)
   template<typename F = Field, std::enable_if_t<fc::object_reflection<F>::is_defined, bool> = true>
   cow_object<Field> operator()() const { return cow_object<Field>(get(), data); }

   /// Conversion to Field& (same as @ref get)
   template<typename F = Field, std::enable_if_t<!fc::object_reflection<F>::is_defined, bool> = true>
   operator const Field&() const { return get(); }

   /// Assign from Field
   Field& operator=(const Field& value) { return set() = value; }
   /// Move-assign from Field
   Field& operator=(Field&& value) { return set() = std::move(value); }
   /// Arrow operator (for mutable access to the field)
   Field* operator->() { return &set(); }
   /// Subscript operator (always mutable, only available if Field has a subscript operator)
   template<typename Arg, typename = make_void<decltype(std::declval<Field>()[std::declval<Arg>()])>>
   decltype(auto) operator[](Arg&& arg) { return set()[std::forward<Arg>(arg)]; }
};
} // namespace impl

template<typename T>
struct cow_object : fc::object_reflection<const T, impl::cow_refletion_data, impl::cow_field_reflection> {
   cow_object(const T& ref, impl::cow_refletion_data* data)
      : fc::object_reflection<const T, impl::cow_refletion_data, impl::cow_field_reflection>(ref, data) {}

   operator T&() {
      if (!this->_data_->written)
         this->_data_->written = this->_data_->db.get_object(this->_data_->object_id).clone();
      return this->_data_->template get_written<T>();
   }
   operator const T&() const {
      if (!this->_data_->written)
         return this->_data_->template get_written<T>();
      return this->_ref_;
   }
};

/**
 * @brief A wrapper of chain::database which returns writeable objects with copy-on-write logic and the ability to
 * commit all writes to the database later on
 *
 * This class was created as an implementation detail of Tanks and Taps; however, it is not specific to TNT in any
 * way and it could be moved to the db library in the future if this is deemed desirable. To do this, however, it
 * would be necessary to move object types' reflection information back to the header so it can be accessed by code
 * outside the xyz_object.cpp files.
 *
 * The copy-on-write database wrapper returns @ref fc::object_reflection types using @ref cow_field_reflection for
 * field reflections. Client code can interact with these objects in the following ways:
 *
 * \code{.cpp}
 * cow_db_wrapper wrapper(db);
 * auto my_object = my_id(wrapper);
 *
 * // Copy field values:
 * auto my_asset = my_object.asset_field;
 * // or
 * my_asset = my_object.asset_field();
 *
 * // Write field values
 * my_object.asset_field = asset(100);
 *
 * // Const field method access via call operator (does not cause a copy)
 * my_object.complex_field().method();
 *
 * // Mutable field method access via arrow operator (this triggers a copy!)
 * my_object.complex_field->modify();
 *
 * // If the field provides a subscript operator, this can also be used, but this always triggers a copy
 * my_object.map_field["key"] = value;
 *
 * // To write all changes to the database, call @ref commit
 * wrapper.commit(mutable_db);
 * \endcode
 */
class cow_db_wrapper {
   const database& db;
   mutable std::map<object_id_type, object*> db_cache;
   mutable std::set<impl::cow_refletion_data, impl::cow_data_lt> pensive_cattle;

public:
   cow_db_wrapper(const database& wrapped_db) : db(wrapped_db) {}

   const database& get_db() const { return db; }

   template<typename T>
   cow_object<T> get(object_id_type id) const {
      decltype(pensive_cattle)::iterator itr = pensive_cattle.find(id);
      if (itr == pensive_cattle.end())
         itr = pensive_cattle.insert(impl::cow_refletion_data::create<T>(db, id)).first;
      auto cache_itr = db_cache.lower_bound(id);
      if (cache_itr == db_cache.end() || cache_itr->first != id)
         cache_itr = db_cache.insert(cache_itr, std::make_pair(id, (object*)&db.get<T>(id)));
      const T* ptr = dynamic_cast<const T*>(cache_itr->second);
      FC_ASSERT(ptr != nullptr, "INTERNAL ERROR: Failed to downcast object. Please report this error.");
      return cow_object<T>(*ptr, const_cast<impl::cow_refletion_data*>(&*itr));
   }
   template<uint8_t SpaceID, uint8_t TypeID>
   auto get(object_id<SpaceID, TypeID> id) const {
      using Object = object_downcast_t<decltype(id)>;
      auto itr = pensive_cattle.find(id);
      if (itr == pensive_cattle.end())
         itr = pensive_cattle.insert(impl::cow_refletion_data::create<Object>(db, id)).first;
      auto cache_itr = db_cache.lower_bound(id);
      if (cache_itr == db_cache.end() || cache_itr->first != id)
         cache_itr = db_cache.insert(cache_itr, std::make_pair(object_id_type(id), (object*)&db.get(id)));
      const Object* ptr = dynamic_cast<const Object*>(cache_itr->second);
      FC_ASSERT(ptr != nullptr, "INTERNAL ERROR: Failed to downcast object. Please report this error.");
      return cow_object<Object>(*ptr, const_cast<impl::cow_refletion_data*>(&*itr));
   }

   /// Write all changes to the database
   void commit(database& mutable_db) {
      static_assert(!std::is_const<decltype(pensive_cattle)>::value, "");
      for (auto& const_cow : pensive_cattle) {
         // I do not understand why cow is const... compiler bug? Fix it.
         auto& cow = const_cast<impl::cow_refletion_data&>(const_cow);
         if (cow.written == nullptr) continue;
         FC_ASSERT(cow.update, "LOGIC ERROR: Update method not set on copy-on-write data. Please report this error.");
         cow.update(cow, mutable_db);
      }
   }
};

} } // namespace graphene::chain
