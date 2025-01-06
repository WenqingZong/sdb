#ifndef SDB_STOPPOINT_COLLECTION_HPP
#define SDB_STOPPOINT_COLLECTION_HPP

#include <algorithm>
#include <libsdb/error.hpp>
#include <libsdb/types.hpp>
#include <memory>
#include <vector>

namespace sdb {
template <class Stoppoint> class stoppoint_collection {
  public:
    Stoppoint& push(std::unique_ptr<Stoppoint> bs);

    /*
    We nned the typename keyword for any types that depend on the Stoppoint
    template parameter; it tells the compiler that this name refers to a type
    rather than a value.
    */
    bool contains_id(typename Stoppoint::id_type id) const;
    bool contains_address(virt_addr address) const;
    bool enabled_stoppoint_at_address(virt_addr address) const;

    Stoppoint& get_by_id(typename Stoppoint::id_type id);
    const Stoppoint& get_by_id(typename Stoppoint::id_type id) const;
    Stoppoint& get_by_address(virt_addr address);
    const Stoppoint& get_by_address(virt_addr address) const;

    void remove_by_id(typename Stoppoint::id_type id);
    void remove_by_address(virt_addr address);

    template <class F> void for_each(F f);
    template <class F> void for_each(F f) const;

    std::size_t size() const { return stoppoints_.size(); }
    bool empty() const { return stoppoints_.empty(); }

  private:
    using points_t = std::vector<std::unique_ptr<Stoppoint>>;

    typename points_t::iterator find_by_id(typename Stoppoint::id_type id);
    typename points_t::const_iterator
    find_by_id(typename Stoppoint::id_type id) const;
    typename points_t::iterator find_by_address(virt_addr address);
    typename points_t::const_iterator find_by_address(virt_addr address) const;

    points_t stoppoints_;
};

template <class Stoppoint>
Stoppoint&
stoppoint_collection<Stoppoint>::push(std::unique_ptr<Stoppoint> bs) {
    stoppoints_.push_back(std::move(bs));
    return *stoppoints_.back();
}

/*
Traditional syntax for function return type:
    ReturnType functionName(args...) {...}
Trailing return syntax:
    auto functionName(Args...) -> ReturnType {...}
Trailing return syntax is useful in templates, because return type depends on
the parameter.
*/
template <class Stoppoint>
auto stoppoint_collection<Stoppoint>::find_by_id(typename Stoppoint::id_type id)
    -> typename points_t::iterator {
    return std::find_if(begin(stoppoints_), end(stoppoints_),
                        [=](auto& point) { return point->id() == id; });
}

template <class Stoppoint>
auto stoppoint_collection<Stoppoint>::find_by_id(
    typename Stoppoint::id_type id) const -> typename points_t::const_iterator {
    return const_cast<stoppoint_collection*>(this)->find_by_id(id);
}

template <class Stoppoint>
auto stoppoint_collection<Stoppoint>::find_by_address(virt_addr address) ->
    typename points_t::iterator {
    return std::find_if(begin(stoppoints_), end(stoppoints_), [=](auto& point) {
        return point->at_address(address);
    });
}

template <class Stoppoint>
auto stoppoint_collection<Stoppoint>::find_by_address(virt_addr address) const
    -> typename points_t::const_iterator {
    return const_cast<stoppoint_collection*>(this)->find_by_address(address);
}

template <class Stoppoint>
bool stoppoint_collection<Stoppoint>::contains_id(
    typename Stoppoint::id_type id) const {
    return find_by_id(id) != end(stoppoints_);
}

template <class Stoppoint>
bool stoppoint_collection<Stoppoint>::contains_address(
    virt_addr address) const {
    return find_by_address(address) != end(stoppoints_);
}

template <class Stoppoint>
bool stoppoint_collection<Stoppoint>::enabled_stoppoint_at_address(
    virt_addr address) const {
    return contains_address(address) and get_by_address(address).is_enabled();
}

template <class Stoppoint>
Stoppoint&
stoppoint_collection<Stoppoint>::get_by_id(typename Stoppoint::id_type id) {
    auto it = find_by_id(id);
    if (it == end(stoppoints_)) {
        error::send("Invalid stoppoint id");
    }
    return **it;
}

template <class Stoppoint>
const Stoppoint& stoppoint_collection<Stoppoint>::get_by_id(
    typename Stoppoint::id_type id) const {
    return const_cast<stoppoint_collection*>(this)->get_by_id(id);
}

} // namespace sdb

#endif