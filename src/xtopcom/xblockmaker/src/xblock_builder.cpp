﻿// Copyright (c) 2017-2018 Telos Foundation & contributors
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string>
#include "xblockmaker/xblock_builder.h"
#include "xconfig/xpredefined_configurations.h"
#include "xconfig/xconfig_register.h"
#include "xdata/xblockbuild.h"

NS_BEG2(top, blockmaker)

void xunitbuildber_txkeys_mgr_t::add_txkey(const std::string & address, const base::xvtxkey_t & txkey) {    
    auto iter = m_account_txkeys.find(address);
    if (iter == m_account_txkeys.end()) {
        base::xvtxkey_vec_t txkeys;
        txkeys.push_back(txkey);
        m_account_txkeys[address] = txkeys;
    } else {
        iter->second.push_back(txkey);
    }
}

void xunitbuildber_txkeys_mgr_t::add_pack_tx(const data::xcons_transaction_ptr_t & tx) {
    {
        const std::string & address = tx->get_account_addr();
        base::xvtxkey_t txkey(tx->get_tx_hash(), tx->get_tx_subtype());
        add_txkey(address, txkey);
    }

    if (tx->is_send_tx() && tx->get_inner_table_flag()) { // TODO(jimmy) add recvtx key to recvaddress
        const std::string & address = tx->get_target_addr();
        base::xvtxkey_t txkey(tx->get_tx_hash(), base::enum_transaction_subtype_recv);
        add_txkey(address, txkey);
    }
}

base::xvtxkey_vec_t xunitbuildber_txkeys_mgr_t::get_account_txkeys(const std::string & address) {
    auto iter = m_account_txkeys.find(address);
    if (iter != m_account_txkeys.end()) {
        return iter->second;
    } else {
        return {};
    }
}

bool xunitbuilder_t::can_make_full_unit(const data::xblock_ptr_t & prev_block) {
    uint64_t current_height = prev_block->get_height() + 1;
    uint64_t current_fullunit_height = prev_block->get_block_class() == base::enum_xvblock_class_full ? prev_block->get_height() : prev_block->get_last_full_block_height();
    uint64_t current_lightunit_count = current_height - current_fullunit_height;

    uint64_t max_limit_lightunit_count = XGET_ONCHAIN_GOVERNANCE_PARAMETER(fullunit_contain_of_unit_num);
    if (current_lightunit_count >= max_limit_lightunit_count) {
        return true;
    }
    return false;
}

data::xblock_ptr_t  xunitbuilder_t::make_block(const data::xblock_ptr_t & prev_block, const data::xunitstate_ptr_t & unitstate, const xunitbuilder_para_t & unitbuilder_para, const data::xblock_consensus_para_t & cs_para){
    std::string binlog = unitstate->take_binlog();
    std::string snapshot = unitstate->take_snapshot();
    if (binlog.empty() || snapshot.empty()) {
        xerror("xunitbuilder_t::make_block fail-invalid unitstate.");
        return nullptr;
    }
    data::xunit_block_para_t bodypara;
    bodypara.set_binlog(binlog);
    bodypara.set_fullstate_bin(snapshot);
    bodypara.set_txkeys(unitbuilder_para.get_txkeys());

    std::shared_ptr<base::xvblockmaker_t> vblockmaker;
    if (xunitbuilder_t::can_make_full_unit(prev_block)) {
        vblockmaker = std::make_shared<data::xfullunit_build_t>(prev_block.get(), bodypara, cs_para);
    } else {
        vblockmaker = std::make_shared<data::xlightunit_build_t>(prev_block.get(), bodypara, cs_para);
    }
    base::xauto_ptr<base::xvblock_t> _new_block = vblockmaker->build_new_block();
    data::xblock_ptr_t proposal_block = data::xblock_t::raw_vblock_to_object_ptr(_new_block.get());
    xinfo("xunitbuilder_t::make_block unit=%s,binlog=%zu,snapshot=%zu,records=%zu,size=%zu,%zu", 
        proposal_block->dump().c_str(), binlog.size(), snapshot.size(), unitstate->get_canvas_records_size(),
        proposal_block->get_input()->get_resources_data().size(), proposal_block->get_output()->get_resources_data().size());
    return proposal_block;
}

void xtablebuilder_t::make_table_prove_property_hashs(base::xvbstate_t* bstate, std::map<std::string, std::string> & property_hashs) {
    std::string property_receiptid_bin = data::xtable_bstate_t::get_receiptid_property_bin(bstate);
    if (!property_receiptid_bin.empty()) {
        uint256_t hash = utl::xsha2_256_t::digest(property_receiptid_bin);
        XMETRICS_GAUGE(metrics::cpu_hash_256_receiptid_bin_calc, 1);
        std::string prophash = std::string(reinterpret_cast<char*>(hash.data()), hash.size());
        property_hashs[data::xtable_bstate_t::get_receiptid_property_name()] = prophash;
    }
}

bool     xtablebuilder_t::update_account_index_property(const data::xtablestate_ptr_t & tablestate, 
                                                        const std::vector<xblock_ptr_t> & batch_units,
                                                        const std::vector<data::xlightunit_tx_info_ptr_t> & txs_info) {
    std::map<std::string, uint64_t> account_nonce_map;

    for (auto & tx_info : txs_info) {
        if (!tx_info->is_send_tx() && !tx_info->is_self_tx()) {
            continue;
        }
        auto & account = tx_info->get_raw_tx()->get_source_addr();
        auto tx_nonce = tx_info->get_last_trans_nonce() + 1;
        auto it = account_nonce_map.find(account);
        if (it == account_nonce_map.end()) {
            account_nonce_map[account] = tx_nonce;
        } else {
            uint64_t & nonce_tmp = it->second;
            if (tx_nonce > nonce_tmp) {
                account_nonce_map[account] = tx_nonce;
            }
        }
    }

    // make account index property binlog
    for (auto & unit : batch_units) {
        // read old index
        base::xaccount_index_t _old_aindex;
        tablestate->get_account_index(unit->get_account(), _old_aindex);
        // update unconfirm sendtx flag
        // bool has_unconfirm_sendtx = _old_aindex.is_has_unconfirm_tx();
        bool has_unconfirm_sendtx = false;  // TODO(jimmy)
        uint64_t nonce = _old_aindex.get_latest_tx_nonce();
        // if (unit->get_block_class() == base::enum_xvblock_class_full) {
        //     has_unconfirm_sendtx = false;
        // } else if (unit->get_block_class() == base::enum_xvblock_class_light) {
        //     has_unconfirm_sendtx = unit->get_unconfirm_sendtx_num() != 0;
        // }
        // update light-unit consensus flag, light-unit must push to committed status for receipt make
        base::enum_xblock_consensus_type _cs_type = _old_aindex.get_latest_unit_consensus_type();
        if (unit->get_block_class() == base::enum_xvblock_class_light) {
            _cs_type = base::enum_xblock_consensus_flag_authenticated;  // if light-unit, reset to authenticated
        } else {
            if (_cs_type == base::enum_xblock_consensus_flag_authenticated) {  // if other-unit, update type
                _cs_type = base::enum_xblock_consensus_flag_locked;
            } else if (_cs_type == base::enum_xblock_consensus_flag_locked) {
                _cs_type = base::enum_xblock_consensus_flag_committed;
            } else if (_cs_type == base::enum_xblock_consensus_flag_committed) {
                // do nothing
            }
        }
        
        auto it = account_nonce_map.find(unit->get_account());
        if (it != account_nonce_map.end()) {
            uint64_t & nonce_tmp = it->second;
            xassert(nonce_tmp > nonce);
            if (nonce_tmp > nonce) {
                nonce = nonce_tmp;
            }
        }

        base::xaccount_index_t _new_aindex(unit.get(), has_unconfirm_sendtx, _cs_type, false, nonce);
        tablestate->set_account_index(unit->get_account(), _new_aindex);
        xdbg("xtablebuilder_t::update_account_index_property account:%s,index=%s", unit->get_account().c_str(), _new_aindex.dump().c_str());
    }
    return true;
}

bool     xtablebuilder_t::update_account_index_property(const data::xtablestate_ptr_t & tablestate, 
                                                        const xblock_ptr_t & unit,
                                                        const data::xunitstate_ptr_t & unit_state) {
    uint64_t nonce = unit_state->account_send_trans_number();

    // make account index property binlog
    // read old index
    base::xaccount_index_t _old_aindex;
    tablestate->get_account_index(unit->get_account(), _old_aindex);
    // update unconfirm sendtx flag
    // bool has_unconfirm_sendtx = _old_aindex.is_has_unconfirm_tx();
    bool has_unconfirm_sendtx = false;  // TODO(jimmy)
    base::enum_xblock_consensus_type _cs_type = _old_aindex.get_latest_unit_consensus_type();
    if (unit->get_block_class() == base::enum_xvblock_class_light) {
        _cs_type = base::enum_xblock_consensus_flag_authenticated;  // if light-unit, reset to authenticated
    } else {
        if (_cs_type == base::enum_xblock_consensus_flag_authenticated) {  // if other-unit, update type
            _cs_type = base::enum_xblock_consensus_flag_locked;
        } else if (_cs_type == base::enum_xblock_consensus_flag_locked) {
            _cs_type = base::enum_xblock_consensus_flag_committed;
        } else if (_cs_type == base::enum_xblock_consensus_flag_committed) {
            // do nothing
        }
    }

    base::xaccount_index_t _new_aindex(unit.get(), has_unconfirm_sendtx, _cs_type, false, nonce);
    tablestate->set_account_index(unit->get_account(), _new_aindex);
    xdbg("xtablebuilder_t::update_account_index_property account:%s,index=%s", unit->get_account().c_str(), _new_aindex.dump().c_str());
    return true;
}

bool     xtablebuilder_t::update_receipt_confirmids(const data::xtablestate_ptr_t & tablestate, 
                                            const std::map<base::xtable_shortid_t, uint64_t> & changed_confirm_ids) {
    for (auto & confirmid_pair : changed_confirm_ids) {
        xdbg("xtablebuilder_t::update_receipt_confirmids set confirmid,self:%d,peer:%d,receiptid:%llu,proposal height:%llu",
             tablestate->get_receiptid_state()->get_self_tableid(),
             confirmid_pair.first,
             confirmid_pair.second,
             tablestate->get_block_height());
        base::xreceiptid_pair_t receiptid_pair;
        tablestate->find_receiptid_pair(confirmid_pair.first, receiptid_pair);
        if (confirmid_pair.second > receiptid_pair.get_confirmid_max() && confirmid_pair.second <= receiptid_pair.get_sendid_max()) {
            receiptid_pair.set_confirmid_max(confirmid_pair.second);
            tablestate->set_receiptid_pair(confirmid_pair.first, receiptid_pair);  // save to modified pairs
        } else {
            xerror("xtablebuilder_t::update_receipt_confirmids set confirmid,self:%d,peer:%d,receiptid:%llu,proposal height:%llu,receiptid_pair=%s",
                tablestate->get_receiptid_state()->get_self_tableid(),
                confirmid_pair.first,
                confirmid_pair.second,
                tablestate->get_block_height(),
                receiptid_pair.dump().c_str());
            return false;
        }
    }
    return true;
}

void     xtablebuilder_t::make_table_block_para(const std::vector<xblock_ptr_t> & batch_units,
                                                const data::xtablestate_ptr_t & tablestate,
                                                txexecutor::xexecute_output_t const& execute_output, 
                                                data::xtable_block_para_t & lighttable_para) {
    int64_t tgas_balance_change = 0;
    std::vector<data::xlightunit_tx_info_ptr_t> txs_info;
    std::map<std::string, std::string> property_hashs;

    // change to xvaction and calc tgas balance change
    for (auto & txout : execute_output.pack_outputs) {
        txs_info.push_back(data::xblockaction_build_t::build_tx_info(txout.m_tx));
        tgas_balance_change += txout.m_vm_output.m_tgas_balance_change;
        for (auto & v : txout.m_vm_output.m_contract_create_txs) {
            txs_info.push_back(data::xblockaction_build_t::build_tx_info(v));
        }
    }

    // make receiptid property hashs
    xtablebuilder_t::make_table_prove_property_hashs(tablestate->get_bstate().get(), property_hashs);

    std::string binlog = tablestate->take_binlog();
    std::string snapshot = tablestate->take_snapshot();
    if (binlog.empty() || snapshot.empty()) {
        xassert(false);
    }

    lighttable_para.set_property_binlog(binlog);
    lighttable_para.set_fullstate_bin(snapshot);
    lighttable_para.set_batch_units(batch_units);
    lighttable_para.set_tgas_balance_change(tgas_balance_change);
    lighttable_para.set_property_hashs(property_hashs);
    lighttable_para.set_txs(txs_info);
}

data::xblock_ptr_t  xtablebuilder_t::make_light_block(const data::xblock_ptr_t & prev_block, const data::xblock_consensus_para_t & cs_para, data::xtable_block_para_t const& lighttable_para) {
    data::xlighttable_build_t bbuild(prev_block.get(), lighttable_para, cs_para);
    base::xauto_ptr<base::xvblock_t> _new_block = bbuild.build_new_block();
    data::xblock_ptr_t proposal_block = data::xblock_t::raw_vblock_to_object_ptr(_new_block.get());
    return proposal_block;
}

NS_END2
