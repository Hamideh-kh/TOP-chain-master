// Copyright (c) 2017-2018 Telos Foundation & contributors
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "xbasic/xcodec/xmsgpack/xid_codec.hpp"
#include "xdata/xcodec/xmsgpack/xelection/xv2/xelection_group_result_codec.hpp"
#include "xdata/xelection/xv2/xelection_cluster_result.h"

NS_BEG1(msgpack)
MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS) {
NS_BEG1(adaptor)

XINLINE_CONSTEXPR std::size_t xelection_cluster_result_field_count{ 1 };
XINLINE_CONSTEXPR std::size_t xelection_cluster_result_results_index{ 0 };

template <>
struct convert<top::data::election::v2::xelection_cluster_result_t> final {
    msgpack::object const & operator()(msgpack::object const & o, top::data::election::v2::xelection_cluster_result_t & result) const {
        if (o.type != msgpack::type::ARRAY) {
            throw msgpack::type_error{};
        }

        if (o.via.array.size == 0) {
            return o;
        }

        switch (o.via.array.size - 1) {
            default: {
                XATTRIBUTE_FALLTHROUGH;
            }
            case xelection_cluster_result_results_index: {
                result.results(o.via.array.ptr[xelection_cluster_result_results_index].as<std::map<top::common::xgroup_id_t, top::data::election::v2::xelection_group_result_t>>());
                XATTRIBUTE_FALLTHROUGH;
            }
        }

        return o;
    }
};

template <>
struct pack<::top::data::election::v2::xelection_cluster_result_t> {
    template <typename StreamT>
    msgpack::packer<StreamT> & operator()(msgpack::packer<StreamT> & o, top::data::election::v2::xelection_cluster_result_t const & result) const {
        o.pack_array(xelection_cluster_result_field_count);
        o.pack(result.results());

        return o;
    }
};

template <>
struct object_with_zone<top::data::election::v2::xelection_cluster_result_t> {
    void operator()(msgpack::object::with_zone & o, top::data::election::v2::xelection_cluster_result_t const & result) const {
        o.type = msgpack::type::ARRAY;
        o.via.array.size = xelection_cluster_result_field_count;
        o.via.array.ptr = static_cast<msgpack::object *>(o.zone.allocate_align(sizeof(::msgpack::object) * o.via.array.size));
        o.via.array.ptr[xelection_cluster_result_results_index] = msgpack::object{ result.results(), o.zone };
    }
};

NS_END1
}
NS_END1
