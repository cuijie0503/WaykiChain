// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wasmcontracttx.h"

#include "commons/serialize.h"
#include "crypto/hash.h"
#include "main.h"
#include "miner/miner.h"
#include "persistence/contractdb.h"
#include "persistence/txdb.h"
#include "config/version.h"
#include <sstream>

#include "wasm/wasm_context.hpp"
#include "wasm/exceptions.hpp"
#include "wasm/types/name.hpp"
#include "wasm/abi_def.hpp"
#include "wasm/wasm_config.hpp"
#include "wasm/abi_serializer.hpp"
#include "wasm/wasm_native_contract_abi.hpp"
#include "wasm/wasm_native_contract.hpp"


map<UnsignedCharArray, uint64_t> &get_signatures_cache(){
    //fixme:this map should be in maxsize to protect memory
    static map<UnsignedCharArray, uint64_t> signatures_cache;
    return signatures_cache;
}

inline void add_signature_to_cache(const UnsignedCharArray& signature, const uint64_t& account ){
    get_signatures_cache()[signature] = account;
}

inline bool get_signature_from_cache(const UnsignedCharArray& signature, uint64_t& account){
    auto itr = get_signatures_cache().find(signature);
    if(itr != get_signatures_cache().end()){
        account = itr->second;
        return true;
    }
    return false;
}

void to_variant( const wasm::permission &t, json_spirit::Value &v ) {

    json_spirit::Object obj;

    json_spirit::Value val;
    to_variant(wasm::name(t.account), val);
    json_spirit::Config::add(obj, "account", val);

    to_variant(wasm::name(t.perm), val);
    json_spirit::Config::add(obj, "permission", val);

    v = obj;
}


void to_variant( const wasm::inline_transaction &t, json_spirit::Value &v , CCacheWrapper &database) {
//void to_variant( const wasm::inline_transaction &t, json_spirit::Value &v , const std::vector<char>& abi) {

    json_spirit::Object obj;

    json_spirit::Value val;
    to_variant(wasm::name(t.contract), val);
    json_spirit::Config::add(obj, "contract", val);

    to_variant(wasm::name(t.action), val);
    json_spirit::Config::add(obj, "action", val);

    json_spirit::Array arr;
    for (const auto &auth :t.authorization) {
        json_spirit::Value tmp;
        to_variant(auth, tmp);
        arr.push_back(tmp);
    }
    json_spirit::Config::add(obj, "authorization", json_spirit::Value(arr));

    std::vector<char> abi;
    if(!get_native_contract_abi(t.contract, abi)){
        //should be lock
        CUniversalContract contract;

        CAccount contract_account;
        if(database.accountCache.GetAccount(CNickID(wasm::name(t.contract).to_string()), contract_account)
                    && database.contractCache.GetContract(contract_account.regid, contract))
            abi.insert(abi.end(), contract.abi.begin(), contract.abi.end());
    }

    if (abi.size() > 0 && t.action != wasm::N(setcode)) {
        if(t.data.size() > 0){
            try{
                val = wasm::abi_serializer::unpack(abi, wasm::name(t.action).to_string(), t.data,
                                               max_serialization_time);
            } catch (...) {
                to_variant(ToHex(t.data,""), val);
            }
        }
    } else
        to_variant(ToHex(t.data,""), val);

    json_spirit::Config::add(obj, "data", val);

    v = obj;
}


void to_variant( const wasm::inline_transaction_trace &t, json_spirit::Value &v, CCacheWrapper &database) {
//void to_variant( const wasm::inline_transaction_trace &t, json_spirit::Value &v, const std::vector<char>& abi) {
    json_spirit::Object obj;

    json_spirit::Value val;
    to_variant(t.trx_id.ToString(), val);
    json_spirit::Config::add(obj, "trx_id", val);

    // to_variant(t.elapsed.count(), val);
    // json_spirit::Config::add(obj, "elapsed", val);

    to_variant(wasm::name(t.receiver), val);
    json_spirit::Config::add(obj, "receiver", val);

    to_variant(t.trx, val, database);
    json_spirit::Config::add(obj, "trx", val);

    to_variant(t.console, val);
    json_spirit::Config::add(obj, "console", val);

    if (t.inline_traces.size() > 0) {
        json_spirit::Array arr;
        for (const auto &trace :t.inline_traces) {
            json_spirit::Value tmp;
            to_variant(trace, tmp, database);
            arr.push_back(tmp);
        }

        json_spirit::Config::add(obj, "inline_traces", json_spirit::Value(arr));

    }

    v = obj;

}

void to_variant( const wasm::transaction_trace &t, json_spirit::Value &v, CCacheWrapper &database ) {
//void to_variant( const wasm::transaction_trace &t, json_spirit::Value &v, const std::vector<char>& abi ) {

    json_spirit::Object obj;

    json_spirit::Value val;
    to_variant(t.trx_id.ToString(), val);
    json_spirit::Config::add(obj, "trx_id", val);

    to_variant(t.elapsed.count(), val);
    json_spirit::Config::add(obj, "elapsed", val);

    if (t.traces.size() > 0) {
        json_spirit::Array arr;
        for (const auto &trace :t.traces) {
            json_spirit::Value tmp;
            to_variant(trace, tmp, database);
            arr.push_back(tmp);
        }

        json_spirit::Config::add(obj, "traces", json_spirit::Value(arr));
    }

    v = obj;
}

// static void CWasmContractTx::get_abi( uint64_t contract, std::vector<char>& abi, CCacheWrapper &database ){

//     if(!get_native_contract_abi(contract, abi)){
//         //should be lock
//         CUniversalContract contract_store;

//         CAccount contract_account;
//         if(database.accountCache.GetAccount(CNickID(wasm::name(t.contract).to_string()), contract_account)
//                     && database.contractCache.GetContract(contract_account.regid, contract_store))
//             abi.insert(abi.end(), contract_store.abi.begin(), contract_store.abi.end());
//     }

// }

void CWasmContractTx::pause_billing_timer(){

    if(billed_time > chrono::microseconds(0)){
      return;// already paused
    }

    auto now = system_clock::now();
    billed_time = std::chrono::duration_cast<std::chrono::microseconds>(now - pseudo_start);

}

void CWasmContractTx::resume_billing_timer(){

    if(billed_time == chrono::microseconds(0)){
       return;// already release pause
    }
    auto now = system_clock::now();
    pseudo_start = now - billed_time;

    billed_time = chrono::microseconds(0);

}

void CWasmContractTx::validate_contracts(CTxExecuteContext &context){

    auto &database         = *context.pCw;

    for(auto i: inline_transactions){

        wasm::name contract_name     = wasm::name(i.contract);
        //wasm::name contract_action   = wasm::name(i.action);
        if(is_native_contract(contract_name.value)) continue;

        CAccount contract;
        WASM_ASSERT(database.accountCache.GetAccount(nick_name(contract_name.to_string()), contract),
                    account_operation_exception,
                   "CWasmContractTx.contract_validation, contract account does not exist, contract = %s",
                    contract_name.to_string().c_str())

        CUniversalContract contract_store;
        WASM_ASSERT(database.contractCache.GetContract(contract.regid, contract_store),
                    account_operation_exception,
                    "CWasmContractTx.contract_validation, cannot get contract with nick name = %s",
                    contract_name.to_string().c_str())

        WASM_ASSERT(contract_store.code.size() > 0 && contract_store.abi.size() > 0 ,
                    account_operation_exception,
                    "CWasmContractTx.contract_validation, %s contract abi or code  does not exist",
                    contract_name.to_string().c_str())

    }

}

void CWasmContractTx::validate_authorization(const std::vector<uint64_t>& authorization_accounts){

    //authorization in each inlinetransaction must be a subset of signatures from transaction
    for(auto i: inline_transactions){
        for(auto p: i.authorization){
            auto itr = std::find(authorization_accounts.begin(), authorization_accounts.end(), p.account);
            WASM_ASSERT( itr != authorization_accounts.end(),
                         account_operation_exception,
                         "CWasmContractTx.authorization_validation, authorization %s does not have signature",
                         wasm::name(p.account).to_string().c_str())
            // if(p.account != account){
            //     WASM_ASSERT( false,
            //                  account_operation_exception,
            //                  "CWasmContractTx.authorization_validation, authorization %s does not have signature",
            //                  wasm::name(p.account).to_string().c_str())
            // }
        }
    }

}

//bool CWasmContractTx::validate_payer_signature(CTxExecuteContext &context)

void CWasmContractTx::get_accounts_from_signatures(CCacheWrapper &database, std::vector<uint64_t> &authorization_accounts){

    TxID signature_hash = GetHash();
 
    for(auto s:signatures){
        uint64_t authorization_account;
        if(get_signature_from_cache(s.signature, authorization_account)){
            authorization_accounts.push_back(authorization_account);
            continue;
        }
        
        CAccount account;
        WASM_ASSERT( database.accountCache.GetAccount(nick_name(wasm::name(s.account).to_string()), account), 
                     account_operation_exception, "%s",
                     "CWasmContractTx.get_accounts_from_signature, can not get account from public key")

        WASM_ASSERT(account.owner_pubkey.Verify(signature_hash, s.signature),
                    account_operation_exception,
                    "%s",
                    "CWasmContractTx::get_accounts_from_signature, can not get public key from signature")


        authorization_account = wasm::name(account.nickid.ToString()).value;
        add_signature_to_cache(s.signature, authorization_account);

        authorization_accounts.push_back(authorization_account);
    }
    
}

bool CWasmContractTx::CheckTx(CTxExecuteContext &context) {

    try {
        auto &database         = *context.pCw;
        auto &state            = *context.pState;

        WASM_ASSERT(inline_transactions.size() > 0,
                    account_operation_exception,
                    "%s",
                    "CWasmContractTx.CheckTx, Tx must have at least 1 inline_transaction")

        //IMPLEMENT_CHECK_TX_FEE;
        IMPLEMENT_CHECK_TX_REGID(txUid.type());
        validate_contracts(context);

        // uint64_t llFuel = GetFuel(context.height, context.fuel_rate);
        // WASM_ASSERT( llFees >= llFuel, fuel_fee_exception, "%s",
        //             "CWasmContractTx.CheckTx, fee is not enough to afford fuel")
        std::vector<uint64_t> authorization_accounts;
        get_accounts_from_signatures(database, authorization_accounts);
        validate_authorization(authorization_accounts);

        //validate payer
        CAccount payer;
        WASM_ASSERT( database.accountCache.GetAccount(txUid, payer), account_operation_exception, "%s",
                    "CWasmContractTx.CheckTx, get payer failed")
        WASM_ASSERT( payer.HaveOwnerPubKey(), account_operation_exception, "%s",
                    "CWasmContractTx.CheckTx, payer unregistered")
        WASM_ASSERT( find(authorization_accounts.begin(), authorization_accounts.end(), wasm::name(payer.nickid.ToString()).value) != authorization_accounts.end(), 
                    account_operation_exception, 
                    "CWasmContractTx.CheckTx, can not find the signature by payer %s",
                    payer.nickid.ToString().c_str())    

     } catch (wasm::exception &e) {
        return context.pState->DoS(100, ERRORMSG(e.detail()), e.code(), e.detail());
     }

    return true;
}

// bool CWasmContractTx::CheckTx(CTxExecuteContext &context) {

//     try {
//         auto &database         = *context.pCw;
//         auto &state            = *context.pState;

//         WASM_ASSERT(inline_transactions.size() > 0,
//                     account_operation_exception,
//                     "%s",
//                     "CWasmContractTx.CheckTx, Tx must have at least 1 inline_transaction")

//         //IMPLEMENT_CHECK_TX_FEE;
//         IMPLEMENT_CHECK_TX_REGID(txUid.type());
//         contract_validation(context);

//         // uint64_t llFuel = GetFuel(context.height, context.fuel_rate);
//         // WASM_ASSERT( llFees >= llFuel, fuel_fee_exception, "%s",
//         //             "CWasmContractTx.CheckTx, fee is not enough to afford fuel")

//         CAccount account;
//         WASM_ASSERT( database.accountCache.GetAccount(txUid, account), account_operation_exception, "%s",
//                     "CWasmContractTx.CheckTx, get account failed")
//         WASM_ASSERT( account.HaveOwnerPubKey(), account_operation_exception, "%s",
//                     "CWasmContractTx.CheckTx, account unregistered")

//         IMPLEMENT_CHECK_TX_SIGNATURE(account.owner_pubkey);
//         authorization_validation({wasm::name(account.nickid.ToString()).value});

//      } catch (wasm::exception &e) {
//         return context.pState->DoS(100, ERRORMSG(e.detail()), e.code(), e.detail());
//      }

//     return true;
// }

static uint64_t get_fuel_limit(CBaseTx &tx, CTxExecuteContext &context) {

    uint64_t fuel_rate = context.fuel_rate;
    WASM_ASSERT( fuel_rate > 0, fuel_fee_exception, "%s", "get_fuel_limit, fuel_rate cannot be 0")

    uint64_t min_fee;
    WASM_ASSERT( GetTxMinFee(tx.nTxType, context.height, tx.fee_symbol, min_fee),
                 fuel_fee_exception, "%s", "get_fuel_limit, get minFee failed")

    assert(tx.llFees >= min_fee);

    uint64_t fee_for_miner = min_fee * CONTRACT_CALL_RESERVED_FEES_RATIO / 100;
    uint64_t fee_for_gas   = tx.llFees - fee_for_miner;

    uint64_t fuel_limit = std::min<uint64_t>((fee_for_gas / fuel_rate) * 100, MAX_BLOCK_RUN_STEP);

    WASM_ASSERT( fuel_limit > 0, fuel_fee_exception, "%s", "get_fuel_limit, fuel limit equal 0")

    return fuel_limit;
}

static void inline_trace_to_receipts(const wasm::inline_transaction_trace trace, vector<CReceipt>& receipts ){

    if(trace.trx.contract == wasmio_bank && trace.trx.action == wasm::N(transfer)){
        CReceipt receipt;
        receipt.code = TRANSFER_ACTUAL_COINS;

        std::tuple<uint64_t, uint64_t, wasm::asset, string> transfer_data = wasm::unpack<std::tuple<uint64_t, uint64_t, wasm::asset, string>>(trace.trx.data);
        auto from     = std::get<0>(transfer_data);
        auto to       = std::get<1>(transfer_data);
        auto quantity = std::get<2>(transfer_data);
        auto memo     = std::get<3>(transfer_data);

        receipt.from_uid = CUserID(CNickID(wasm::name(from).to_string()));
        receipt.to_uid   = CUserID(CNickID(wasm::name(to).to_string()));

        receipt.coin_symbol = quantity.sym.code().to_string();
        receipt.coin_amount = quantity.amount;

        receipts.push_back(receipt);
    }

    for(auto t: trace.inline_traces){
        inline_trace_to_receipts(t, receipts);
    }

}

static void trace_to_receipts(const wasm::transaction_trace trace, vector<CReceipt>& receipts ){
    for(auto t: trace.traces){
        inline_trace_to_receipts(t, receipts);
    }

}

bool CWasmContractTx::ExecuteTx(CTxExecuteContext &context) {

    try {
        auto &database         = *context.pCw;
        auto execute_tx_return = context.pState;

        mining                    = context.is_mining;
        validating_tx_in_mem_pool = context.is_validating_tx_in_mem_pool;
        nRunStep = sizeof(inline_transactions);

        //charger fee
        CAccount payer;
        WASM_ASSERT(database.accountCache.GetAccount(txUid, payer),
                    account_operation_exception,
                    "wasmnativecontract.Setcode, payer does not exist, payer Id = %s",
                    payer.nickid.ToString().c_str())
        auto quantity = wasm::asset(llFees, wasm::symbol(SYMB::WICC, 8));
        sub_balance(payer, quantity, database.accountCache);

        pseudo_start = system_clock::now();

        wasm::transaction_trace trx_trace;
        trx_trace.trx_id = GetHash();
        //trx_trace.block_height = nHeight;
        //trx_trace.block_time =
        vector<CReceipt> receipts;

        for (auto trx: inline_transactions) {
            trx_trace.traces.emplace_back();
            execute_inline_transaction(trx_trace.traces.back(), trx, trx.contract, database, receipts, 0);
        }
        trx_trace.elapsed = std::chrono::duration_cast<std::chrono::microseconds>(system_clock::now() - pseudo_start);

        uint64_t fee = get_fuel_limit(*this, context);

        WASM_ASSERT( fee > nRunStep, fuel_fee_exception, "%s",
                    "CWasmContractTx.ExecuteTx, fee is not enough to afford fuel")

        //database.save_trace(GetHash(),trace);
        std::vector<char> trace_bytes  = wasm::pack<transaction_trace>(trx_trace);
        //transaction_trace t            = wasm::unpack<transaction_trace>(trace_bytes);
        WASM_ASSERT( database.contractCache.SetContractTraces(GetHash(), std::string(trace_bytes.begin(),trace_bytes.end())),
                     wasm_exception,
                     "CWasmContractTx::ExecuteTx, set tx trace failed! txid=%s",
                     GetHash().ToString().c_str())


        trace_to_receipts(trx_trace, receipts);
        WASM_ASSERT( database.txReceiptCache.SetTxReceipts(GetHash(), receipts),
                     wasm_exception,
                     "CWasmContractTx::ExecuteTx, set tx receipts failed! txid=%s",
                     GetHash().ToString().c_str())

        // json_spirit::Value v;
        // to_variant(t, v, database);
        // execute_tx_return->SetReturn(json_spirit::write_formatted(v));
        execute_tx_return->SetReturn(GetHash().ToString());

    } catch (wasm::exception &e) {
        return context.pState->DoS(100, ERRORMSG(e.detail()), e.code(), e.detail());
    }

    return true;
}

void CWasmContractTx::execute_inline_transaction( wasm::inline_transaction_trace &trace,
                                                 wasm::inline_transaction &trx,
                                                 uint64_t receiver,
                                                 CCacheWrapper &database,
                                                 vector<CReceipt> &receipts,
                                                 //CValidationState &state,
                                                 uint32_t recurse_depth ) {

    wasm_context ctx(*this, trx, database, receipts, mining, recurse_depth);

    //check timeout
    WASM_ASSERT(std::chrono::duration_cast<std::chrono::microseconds>(system_clock::now() - pseudo_start) < ctx.get_transaction_duration(),
                wasm_timeout_exception, "%s", "timeout");

    ctx._receiver = receiver;
    ctx.execute(trace);

}


bool CWasmContractTx::GetInvolvedKeyIds( CCacheWrapper &cw, set <CKeyID> &keyIds ) {
    CKeyID senderKeyId;
    if (!cw.accountCache.GetKeyId(txUid, senderKeyId))
        return false;

    keyIds.insert(senderKeyId);
    return true;
}

uint64_t CWasmContractTx::GetFuel(int32_t height, uint32_t nFuelRate) {
    uint64_t minFee = 0;
    if (!GetTxMinFee(nTxType, height, fee_symbol, minFee)) {
        LogPrint(BCLog::ERROR, "CWasmContractTx::GetFuel(), get min_fee failed! fee_symbol=%s\n", fee_symbol);
        throw runtime_error("CWasmContractTx::GetFuel(), get min_fee failed");
    }

    return std::max<uint64_t>(((nRunStep / 100.0f) * nFuelRate), minFee);
}


string CWasmContractTx::ToString( CAccountDBCache &accountCache ) {

    if(inline_transactions.size() == 0){
        return string("");
    }

    inline_transaction trx = inline_transactions[0];
    CAccount sender;
    if(!accountCache.GetAccount(txUid, sender)){
        return string("");
    }

    return strprintf(
        "txType=%s, hash=%s, ver=%d, sender=%s, llFees=%llu, contract=%s, action=%s, arguments=%s, "
        "valid_height=%d",
        GetTxType(nTxType), GetHash().ToString(), nVersion, sender.nickid.ToString(), llFees,
        wasm::name(trx.contract).to_string(), wasm::name(trx.action).to_string(),
        HexStr(trx.data), valid_height);
}

Object CWasmContractTx::ToJson( const CAccountDBCache &accountCache ) const {

    if(inline_transactions.size() == 0){ 
        return Object{};
    }

    Object result;
    if(inline_transactions.size() > 0){
        result = CBaseTx::ToJson(accountCache);
        inline_transaction trx = inline_transactions[0];
        result.push_back(Pair("contract",       wasm::name(trx.contract).to_string()));
        result.push_back(Pair("action",         wasm::name(trx.action).to_string()));
        result.push_back(Pair("arguments",      HexStr(trx.data)));

        Array var;
        for(auto s: signatures){
            Object tmp;
            tmp.push_back(Pair("account",       wasm::name(s.account).to_string()));
            tmp.push_back(Pair("signature",     HexStr(s.signature)));
            var.push_back(tmp);
        }
        result.push_back(Pair("signatures", var));


    }

    return result;


    // Object result = CBaseTx::ToJson(accountCache);
    // json_spirit::Array arr;
    // for (const auto &i :inline_transactions) {
    //         json_spirit::Value tmp;
    //         to_variant(i, tmp, accountCache);
    //         arr.push_back(tmp);
    // }
    // json_spirit::Config::add(result, "inline_transactions", json_spirit::Value(arr));

    // return result;

}

