// Copyright (c) 2017-2020 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "addressbook.h"
#include "amount.h"
#include "base58.h"
#include "coincontrol.h"
#include "init.h"
#include "libzerocoin/Coin.h"
#include "net.h"
#include "rpc/server.h"
#include "spork.h"
#include "utilmoneystr.h"
#include "wallet.h"
#include "zpiv/deterministicmint.h"
#include "zpivchain.h"

#include <boost/assign/list_of.hpp>

#include <univalue.h>

UniValue getzerocoinbalance(const JSONRPCRequest& request)
{

    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getzerocoinbalance\n"
            "\nReturn the wallet's total zPIV balance.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nResult:\n"
            "amount         (numeric) Total zPIV balance.\n"

            "\nExamples:\n" +
            HelpExampleCli("getzerocoinbalance", "") + HelpExampleRpc("getzerocoinbalance", ""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked(true);

    UniValue ret(UniValue::VOBJ);
    const UniValue& zcBalance = ValueFromAmount(pwalletMain->GetZerocoinBalance());
    ret.pushKV("Total", zcBalance);
    ret.pushKV("Mature", zcBalance);
    ret.pushKV("Unconfirmed", 0);
    ret.pushKV("Immature", 0);
    return ret;

}

UniValue listmintedzerocoins(const JSONRPCRequest& request)
{

    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "listmintedzerocoins (fVerbose) (fMatureOnly)\n"
            "\nList all zPIV mints in the wallet.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. fVerbose      (boolean, optional, default=false) Output mints metadata.\n"
            "2. fMatureOnly   (boolean, optional, default=false) List only mature mints.\n"
            "                 Set only if fVerbose is specified\n"

            "\nResult (with fVerbose=false):\n"
            "[\n"
            "  \"xxx\"      (string) Pubcoin in hex format.\n"
            "  ,...\n"
            "]\n"

            "\nResult (with fVerbose=true):\n"
            "[\n"
            "  {\n"
            "    \"serial hash\": \"xxx\",   (string) Mint serial hash in hex format.\n"
            "    \"version\": n,   (numeric) Zerocoin version number.\n"
            "    \"zPIV ID\": \"xxx\",   (string) Pubcoin in hex format.\n"
            "    \"denomination\": n,   (numeric) Coin denomination.\n"
            "    \"mint height\": n     (numeric) Height of the block containing this mint.\n"
            "    \"confirmations\": n   (numeric) Number of confirmations.\n"
            "    \"hash stake\": \"xxx\",   (string) Mint serialstake hash in hex format.\n"
            "  }\n"
            "  ,..."
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listmintedzerocoins", "") + HelpExampleRpc("listmintedzerocoins", "") +
            HelpExampleCli("listmintedzerocoins", "true") + HelpExampleRpc("listmintedzerocoins", "true") +
            HelpExampleCli("listmintedzerocoins", "true true") + HelpExampleRpc("listmintedzerocoins", "true, true"));

    bool fVerbose = (request.params.size() > 0) ? request.params[0].get_bool() : false;
    bool fMatureOnly = (request.params.size() > 1) ? request.params[1].get_bool() : false;

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked(true);

    CWalletDB walletdb(pwalletMain->strWalletFile);
    std::set<CMintMeta> setMints = pwalletMain->zpivTracker->ListMints(true, fMatureOnly, true);

    int nBestHeight = chainActive.Height();

    UniValue jsonList(UniValue::VARR);
    if (fVerbose) {
        for (auto m : setMints) {
            // Construct mint object
            UniValue objMint(UniValue::VOBJ);
            objMint.pushKV("serial hash", m.hashSerial.GetHex());  // Serial hash
            objMint.pushKV("version", m.nVersion);                 // Zerocoin version
            objMint.pushKV("zPIV ID", m.hashPubcoin.GetHex());     // PubCoin
            int denom = libzerocoin::ZerocoinDenominationToInt(m.denom);
            objMint.pushKV("denomination", denom);                 // Denomination
            objMint.pushKV("mint height", m.nHeight);              // Mint Height
            int nConfirmations = (m.nHeight && nBestHeight > m.nHeight) ? nBestHeight - m.nHeight : 0;
            objMint.pushKV("confirmations", nConfirmations);       // Confirmations
            if (m.hashStake.IsNull()) {
                CZerocoinMint mint;
                if (pwalletMain->GetMint(m.hashSerial, mint)) {
                    uint256 hashStake = mint.GetSerialNumber().getuint256();
                    hashStake = Hash(hashStake.begin(), hashStake.end());
                    m.hashStake = hashStake;
                    pwalletMain->zpivTracker->UpdateState(m);
                }
            }
            objMint.pushKV("hash stake", m.hashStake.GetHex());    // hashStake
            // Push back mint object
            jsonList.push_back(objMint);
        }
    } else {
        for (const CMintMeta& m : setMints)
            // Push back PubCoin
            jsonList.push_back(m.hashPubcoin.GetHex());
    }
    return jsonList;
}

UniValue listzerocoinamounts(const JSONRPCRequest& request)
{

    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "listzerocoinamounts\n"
            "\nGet information about your zerocoin amounts.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"denomination\": n,   (numeric) Denomination Value.\n"
            "    \"mints\": n           (numeric) Number of mints.\n"
            "  }\n"
            "  ,..."
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listzerocoinamounts", "") + HelpExampleRpc("listzerocoinamounts", ""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked(true);

    CWalletDB walletdb(pwalletMain->strWalletFile);
    std::set<CMintMeta> setMints = pwalletMain->zpivTracker->ListMints(true, true, true);

    std::map<libzerocoin::CoinDenomination, CAmount> spread;
    for (const auto& denom : libzerocoin::zerocoinDenomList)
        spread.emplace(denom, 0);
    for (auto& meta : setMints) spread.at(meta.denom)++;


    UniValue ret(UniValue::VARR);
    for (const auto& m : libzerocoin::zerocoinDenomList) {
        UniValue val(UniValue::VOBJ);
        val.pushKV("denomination", libzerocoin::ZerocoinDenominationToInt(m));
        val.pushKV("mints", (int64_t)spread.at(m));
        ret.push_back(val);
    }
    return ret;
}

UniValue listspentzerocoins(const JSONRPCRequest& request)
{

    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "listspentzerocoins\n"
            "\nList all the spent zPIV mints in the wallet.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nResult:\n"
            "[\n"
            "  \"xxx\"      (string) Pubcoin in hex format.\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listspentzerocoins", "") + HelpExampleRpc("listspentzerocoins", ""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked(true);

    CWalletDB walletdb(pwalletMain->strWalletFile);
    std::list<CBigNum> listPubCoin = walletdb.ListSpentCoinsSerial();

    UniValue jsonList(UniValue::VARR);
    for (const CBigNum& pubCoinItem : listPubCoin) {
        jsonList.push_back(pubCoinItem.GetHex());
    }

    return jsonList;
}

UniValue mintzerocoin(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "mintzerocoin amount ( utxos )\n"
            "\nMint the specified zPIV amount\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. amount      (numeric, required) Enter an amount of Piv to convert to zPIV\n"
            "2. utxos       (string, optional) A json array of objects.\n"
            "                   Each object needs the txid (string) and vout (numeric)\n"
            "  [\n"
            "    {\n"
            "      \"txid\":\"txid\",    (string) The transaction id\n"
            "      \"vout\": n         (numeric) The output number\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"

            "\nResult:\n"
            "{\n"
            "   \"txid\": \"xxx\",       (string) Transaction ID.\n"
            "   \"time\": nnn            (numeric) Time to mint this transaction.\n"
            "   \"mints\":\n"
            "   [\n"
            "      {\n"
            "         \"denomination\": nnn,     (numeric) Minted denomination.\n"
            "         \"pubcoin\": \"xxx\",      (string) Pubcoin in hex format.\n"
            "         \"randomness\": \"xxx\",   (string) Hex encoded randomness.\n"
            "         \"serial\": \"xxx\",       (string) Serial in hex format.\n"
            "      },\n"
            "      ...\n"
            "   ]\n"
            "}\n"

            "\nExamples:\n"
            "\nMint 50 from anywhere\n" +
            HelpExampleCli("mintzerocoin", "50") +
            "\nMint 13 from a specific output\n" +
            HelpExampleCli("mintzerocoin", "13 \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
            "\nAs a json rpc call\n" +
            HelpExampleRpc("mintzerocoin", "13, \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\""));


    if (!Params().IsRegTestNet())
        throw JSONRPCError(RPC_WALLET_ERROR, "zPIV minting is DISABLED");

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (request.params.size() == 1)
    {
        RPCTypeCheck(request.params, boost::assign::list_of(UniValue::VNUM));
    } else
    {
        RPCTypeCheck(request.params, boost::assign::list_of(UniValue::VNUM)(UniValue::VARR));
    }

    int64_t nTime = GetTimeMillis();
    if(sporkManager.IsSporkActive(SPORK_16_ZEROCOIN_MAINTENANCE_MODE))
        throw JSONRPCError(RPC_WALLET_ERROR, "zPIV is currently disabled due to maintenance.");

    EnsureWalletIsUnlocked(true);

    CAmount nAmount = request.params[0].get_int() * COIN;

    CWalletTx wtx;
    std::vector<CDeterministicMint> vDMints;
    std::string strError;
    std::vector<COutPoint> vOutpts;

    if (request.params.size() == 2)
    {
        UniValue outputs = request.params[1].get_array();
        for (unsigned int idx = 0; idx < outputs.size(); idx++) {
            const UniValue& output = outputs[idx];
            if (!output.isObject())
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");
            const UniValue& o = output.get_obj();

            RPCTypeCheckObj(o, boost::assign::map_list_of("txid", UniValue::VSTR)("vout", UniValue::VNUM));

            std::string txid = find_value(o, "txid").get_str();
            if (!IsHex(txid))
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");

            int nOutput = find_value(o, "vout").get_int();
            if (nOutput < 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

            COutPoint outpt(uint256S(txid), nOutput);
            vOutpts.push_back(outpt);
        }
        strError = pwalletMain->MintZerocoinFromOutPoint(nAmount, wtx, vDMints, vOutpts);
    } else
    {
        strError = pwalletMain->MintZerocoin(nAmount, wtx, vDMints);
    }

    if (strError != "")
        throw JSONRPCError(RPC_WALLET_ERROR, strError);

    UniValue retObj(UniValue::VOBJ);
    retObj.pushKV("txid", wtx.GetHash().ToString());
    retObj.pushKV("time", GetTimeMillis() - nTime);
    UniValue arrMints(UniValue::VARR);
    for (CDeterministicMint dMint : vDMints) {
        UniValue m(UniValue::VOBJ);
        m.pushKV("denomination", ValueFromAmount(libzerocoin::ZerocoinDenominationToAmount(dMint.GetDenomination())));
        m.pushKV("pubcoinhash", dMint.GetPubcoinHash().GetHex());
        m.pushKV("serialhash", dMint.GetSerialHash().GetHex());
        m.pushKV("seedhash", dMint.GetSeedHash().GetHex());
        m.pushKV("count", (int64_t)dMint.GetCount());
        arrMints.push_back(m);
    }
    retObj.pushKV("mints", arrMints);

    return retObj;
}

UniValue DoZpivSpend(const CAmount nAmount, std::vector<CZerocoinMint>& vMintsSelected, std::string address_str)
{
    int64_t nTimeStart = GetTimeMillis();
    CTxDestination address{CNoDestination()}; // Optional sending address. Dummy initialization here.
    CWalletTx wtx;
    CZerocoinSpendReceipt receipt;
    bool fSuccess;

    std::list<std::pair<CTxDestination, CAmount>> outputs;
    if(address_str != "") { // Spend to supplied destination address
        bool isStaking = false;
        address = DecodeDestination(address_str, isStaking);
        if(!IsValidDestination(address) || isStaking)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid PIVX address");
        outputs.emplace_back(address, nAmount);
    }

    EnsureWalletIsUnlocked();
    fSuccess = pwalletMain->SpendZerocoin(nAmount, wtx, receipt, vMintsSelected, outputs, nullptr);

    if (!fSuccess)
        throw JSONRPCError(RPC_WALLET_ERROR, receipt.GetStatusMessage());

    CAmount nValueIn = 0;
    UniValue arrSpends(UniValue::VARR);
    for (CZerocoinSpend spend : receipt.GetSpends()) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("denomination", spend.GetDenomination());
        obj.pushKV("pubcoin", spend.GetPubCoin().GetHex());
        obj.pushKV("serial", spend.GetSerial().GetHex());
        uint32_t nChecksum = spend.GetAccumulatorChecksum();
        obj.pushKV("acc_checksum", HexStr(BEGIN(nChecksum), END(nChecksum)));
        arrSpends.push_back(obj);
        nValueIn += libzerocoin::ZerocoinDenominationToAmount(spend.GetDenomination());
    }

    CAmount nValueOut = 0;
    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < wtx.vout.size(); i++) {
        const CTxOut& txout = wtx.vout[i];
        UniValue out(UniValue::VOBJ);
        out.pushKV("value", ValueFromAmount(txout.nValue));
        nValueOut += txout.nValue;

        CTxDestination dest;
        if(txout.IsZerocoinMint())
            out.pushKV("address", "zerocoinmint");
        else if(ExtractDestination(txout.scriptPubKey, dest))
            out.pushKV("address", EncodeDestination(dest));
        vout.push_back(out);
    }

    //construct JSON to return
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("txid", wtx.GetHash().ToString());
    ret.pushKV("bytes", (int64_t)wtx.GetTotalSize());
    ret.pushKV("fee", ValueFromAmount(nValueIn - nValueOut));
    ret.pushKV("duration_millis", (GetTimeMillis() - nTimeStart));
    ret.pushKV("spends", arrSpends);
    ret.pushKV("outputs", vout);

    return ret;
}

UniValue spendzerocoin(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2 || request.params.size() < 1)
        throw std::runtime_error(
            "spendzerocoin amount ( \"address\" )\n"
            "\nSpend zPIV to a PIV address.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. amount          (numeric, required) Amount to spend.\n"
            "2. \"address\"     (string, optional, default=change) Send to specified address or to a new change address.\n"
            "                       If there is change then an address is required\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\": \"xxx\",             (string) Transaction hash.\n"
            "  \"bytes\": nnn,              (numeric) Transaction size.\n"
            "  \"fee\": amount,             (numeric) Transaction fee (if any).\n"
            "  \"spends\": [                (array) JSON array of input objects.\n"
            "    {\n"
            "      \"denomination\": nnn,   (numeric) Denomination value.\n"
            "      \"pubcoin\": \"xxx\",      (string) Pubcoin in hex format.\n"
            "      \"serial\": \"xxx\",       (string) Serial number in hex format.\n"
            "      \"acc_checksum\": \"xxx\", (string) Accumulator checksum in hex format.\n"
            "    }\n"
            "    ,...\n"
            "  ],\n"
            "  \"outputs\": [                 (array) JSON array of output objects.\n"
            "    {\n"
            "      \"value\": amount,         (numeric) Value in PIV.\n"
            "      \"address\": \"xxx\"         (string) PIV address or \"zerocoinmint\" for reminted change.\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples\n" +
            HelpExampleCli("spendzerocoin", "5000 \"DMJRSsuU9zfyrvxVaAEFQqK4MxZg6vgeS6\"") +
            HelpExampleRpc("spendzerocoin", "5000 \"DMJRSsuU9zfyrvxVaAEFQqK4MxZg6vgeS6\""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(sporkManager.IsSporkActive(SPORK_16_ZEROCOIN_MAINTENANCE_MODE))
        throw JSONRPCError(RPC_WALLET_ERROR, "zPIV is currently disabled due to maintenance.");

    CAmount nAmount = AmountFromValue(request.params[0]);        // Spending amount
    const std::string address_str = (request.params.size() > 1 ? request.params[1].get_str() : "");

    std::vector<CZerocoinMint> vMintsSelected;
    return DoZpivSpend(nAmount, vMintsSelected, address_str);
}

UniValue spendzerocoinmints(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "spendzerocoinmints mints_list ( \"address\" ) \n"
            "\nSpend zPIV mints to a PIV address.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. mints_list     (string, required) A json array of zerocoin mints serial hashes\n"
            "2. \"address\"     (string, optional, default=change) Send to specified address or to a new change address.\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\": \"xxx\",             (string) Transaction hash.\n"
            "  \"bytes\": nnn,              (numeric) Transaction size.\n"
            "  \"fee\": amount,             (numeric) Transaction fee (if any).\n"
            "  \"spends\": [                (array) JSON array of input objects.\n"
            "    {\n"
            "      \"denomination\": nnn,   (numeric) Denomination value.\n"
            "      \"pubcoin\": \"xxx\",      (string) Pubcoin in hex format.\n"
            "      \"serial\": \"xxx\",       (string) Serial number in hex format.\n"
            "      \"acc_checksum\": \"xxx\", (string) Accumulator checksum in hex format.\n"
            "    }\n"
            "    ,...\n"
            "  ],\n"
            "  \"outputs\": [                 (array) JSON array of output objects.\n"
            "    {\n"
            "      \"value\": amount,         (numeric) Value in PIV.\n"
            "      \"address\": \"xxx\"         (string) PIV address or \"zerocoinmint\" for reminted change.\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples\n" +
            HelpExampleCli("spendzerocoinmints", "'[\"0d8c16eee7737e3cc1e4e70dc006634182b175e039700931283b202715a0818f\", \"dfe585659e265e6a509d93effb906d3d2a0ac2fe3464b2c3b6d71a3ef34c8ad7\"]' \"DMJRSsuU9zfyrvxVaAEFQqK4MxZg6vgeS6\"") +
            HelpExampleRpc("spendzerocoinmints", "[\"0d8c16eee7737e3cc1e4e70dc006634182b175e039700931283b202715a0818f\", \"dfe585659e265e6a509d93effb906d3d2a0ac2fe3464b2c3b6d71a3ef34c8ad7\"], \"DMJRSsuU9zfyrvxVaAEFQqK4MxZg6vgeS6\""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if(sporkManager.IsSporkActive(SPORK_16_ZEROCOIN_MAINTENANCE_MODE))
        throw JSONRPCError(RPC_WALLET_ERROR, "zPIV is currently disabled due to maintenance.");

    UniValue arrMints = request.params[0].get_array();
    const std::string address_str = (request.params.size() > 1 ? request.params[1].get_str() : "");

    if (arrMints.size() == 0)
        throw JSONRPCError(RPC_WALLET_ERROR, "No zerocoin selected");

    // check mints supplied and save serial hash (do this here so we don't fetch if any is wrong)
    std::vector<uint256> vSerialHashes;
    for(unsigned int i = 0; i < arrMints.size(); i++) {
        std::string serialHashStr = arrMints[i].get_str();
        if (!IsHex(serialHashStr))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex serial hash");
        vSerialHashes.push_back(uint256S(serialHashStr));
    }

    // fetch mints and update nAmount
    CAmount nAmount(0);
    std::vector<CZerocoinMint> vMintsSelected;
    for(const uint256& serialHash : vSerialHashes) {
        CZerocoinMint mint;
        if (!pwalletMain->GetMint(serialHash, mint)) {
            std::string strErr = "Failed to fetch mint associated with serial hash " + serialHash.GetHex();
            throw JSONRPCError(RPC_WALLET_ERROR, strErr);
        }
        vMintsSelected.emplace_back(mint);
        nAmount += mint.GetDenominationAsAmount();
    }

    return DoZpivSpend(nAmount, vMintsSelected, address_str);
}

UniValue resetmintzerocoin(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "resetmintzerocoin ( fullscan )\n"
            "\nScan the blockchain for all of the zerocoins that are held in the wallet database.\n"
            "Update any meta-data that is incorrect. Archive any mints that are not able to be found.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. fullscan          (boolean, optional) Rescan each block of the blockchain.\n"
            "                               WARNING - may take 30+ minutes!\n"

            "\nResult:\n"
            "{\n"
            "  \"updated\": [       (array) JSON array of updated mints.\n"
            "    \"xxx\"            (string) Hex encoded mint.\n"
            "    ,...\n"
            "  ],\n"
            "  \"archived\": [      (array) JSON array of archived mints.\n"
            "    \"xxx\"            (string) Hex encoded mint.\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("resetmintzerocoin", "true") + HelpExampleRpc("resetmintzerocoin", "true"));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CWalletDB walletdb(pwalletMain->strWalletFile);
    CzPIVTracker* zpivTracker = pwalletMain->zpivTracker.get();
    std::set<CMintMeta> setMints = zpivTracker->ListMints(false, false, true);
    std::vector<CMintMeta> vMintsToFind(setMints.begin(), setMints.end());
    std::vector<CMintMeta> vMintsMissing;
    std::vector<CMintMeta> vMintsToUpdate;

    // search all of our available data for these mints
    FindMints(vMintsToFind, vMintsToUpdate, vMintsMissing);

    // update the meta data of mints that were marked for updating
    UniValue arrUpdated(UniValue::VARR);
    for (CMintMeta meta : vMintsToUpdate) {
        zpivTracker->UpdateState(meta);
        arrUpdated.push_back(meta.hashPubcoin.GetHex());
    }

    // delete any mints that were unable to be located on the blockchain
    UniValue arrDeleted(UniValue::VARR);
    for (CMintMeta mint : vMintsMissing) {
        zpivTracker->Archive(mint);
        arrDeleted.push_back(mint.hashPubcoin.GetHex());
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("updated", arrUpdated);
    obj.pushKV("archived", arrDeleted);
    return obj;
}

UniValue resetspentzerocoin(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "resetspentzerocoin\n"
            "\nScan the blockchain for all of the zerocoins that are held in the wallet database.\n"
            "Reset mints that are considered spent that did not make it into the blockchain.\n"

            "\nResult:\n"
            "{\n"
            "  \"restored\": [        (array) JSON array of restored objects.\n"
            "    {\n"
            "      \"serial\": \"xxx\"  (string) Serial in hex format.\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("resetspentzerocoin", "") + HelpExampleRpc("resetspentzerocoin", ""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CWalletDB walletdb(pwalletMain->strWalletFile);
    CzPIVTracker* zpivTracker = pwalletMain->zpivTracker.get();
    std::set<CMintMeta> setMints = zpivTracker->ListMints(false, false, false);
    std::list<CZerocoinSpend> listSpends = walletdb.ListSpentCoins();
    std::list<CZerocoinSpend> listUnconfirmedSpends;

    for (CZerocoinSpend spend : listSpends) {
        CTransaction tx;
        uint256 hashBlock = UINT256_ZERO;
        if (!GetTransaction(spend.GetTxHash(), tx, hashBlock)) {
            listUnconfirmedSpends.push_back(spend);
            continue;
        }

        //no confirmations
        if (hashBlock.IsNull())
            listUnconfirmedSpends.push_back(spend);
    }

    UniValue objRet(UniValue::VOBJ);
    UniValue arrRestored(UniValue::VARR);
    for (CZerocoinSpend spend : listUnconfirmedSpends) {
        for (auto& meta : setMints) {
            if (meta.hashSerial == GetSerialHash(spend.GetSerial())) {
                zpivTracker->SetPubcoinNotUsed(meta.hashPubcoin);
                walletdb.EraseZerocoinSpendSerialEntry(spend.GetSerial());
                RemoveSerialFromDB(spend.GetSerial());
                UniValue obj(UniValue::VOBJ);
                obj.pushKV("serial", spend.GetSerial().GetHex());
                arrRestored.push_back(obj);
                continue;
            }
        }
    }

    objRet.pushKV("restored", arrRestored);
    return objRet;
}

UniValue getarchivedzerocoin(const JSONRPCRequest& request)
{
    if(request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getarchivedzerocoin\n"
            "\nDisplay zerocoins that were archived because they were believed to be orphans.\n"
            "Provides enough information to recover mint if it was incorrectly archived.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txid\": \"xxx\",           (string) Transaction ID for archived mint.\n"
            "    \"denomination\": amount,  (numeric) Denomination value.\n"
            "    \"serial\": \"xxx\",         (string) Serial number in hex format.\n"
            "    \"randomness\": \"xxx\",     (string) Hex encoded randomness.\n"
            "    \"pubcoin\": \"xxx\"         (string) Pubcoin in hex format.\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getarchivedzerocoin", "") + HelpExampleRpc("getarchivedzerocoin", ""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    CWalletDB walletdb(pwalletMain->strWalletFile);
    std::list<CZerocoinMint> listMints = walletdb.ListArchivedZerocoins();
    std::list<CDeterministicMint> listDMints = walletdb.ListArchivedDeterministicMints();

    UniValue arrRet(UniValue::VARR);
    for (const CZerocoinMint& mint : listMints) {
        UniValue objMint(UniValue::VOBJ);
        objMint.pushKV("txid", mint.GetTxHash().GetHex());
        objMint.pushKV("denomination", ValueFromAmount(mint.GetDenominationAsAmount()));
        objMint.pushKV("serial", mint.GetSerialNumber().GetHex());
        objMint.pushKV("randomness", mint.GetRandomness().GetHex());
        objMint.pushKV("pubcoin", mint.GetValue().GetHex());
        arrRet.push_back(objMint);
    }

    for (const CDeterministicMint& dMint : listDMints) {
        UniValue objDMint(UniValue::VOBJ);
        objDMint.pushKV("txid", dMint.GetTxHash().GetHex());
        objDMint.pushKV("denomination", ValueFromAmount(libzerocoin::ZerocoinDenominationToAmount(dMint.GetDenomination())));
        objDMint.pushKV("serialhash", dMint.GetSerialHash().GetHex());
        objDMint.pushKV("pubcoinhash", dMint.GetPubcoinHash().GetHex());
        objDMint.pushKV("seedhash", dMint.GetSeedHash().GetHex());
        objDMint.pushKV("count", (int64_t)dMint.GetCount());
        arrRet.push_back(objDMint);
    }

    return arrRet;
}

UniValue exportzerocoins(const JSONRPCRequest& request)
{
    if(request.fHelp || request.params.empty() || request.params.size() > 2)
        throw std::runtime_error(
            "exportzerocoins include_spent ( denomination )\n"
            "\nExports zerocoin mints that are held by the current wallet file\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"include_spent\"        (bool, required) Include mints that have already been spent\n"
            "2. \"denomination\"         (integer, optional) Export a specific denomination of zPIV\n"

            "\nResult:\n"
            "[                   (array of json object)\n"
            "  {\n"
            "    \"id\": \"serial hash\",  (string) the mint's zPIV serial hash \n"
            "    \"d\": n,         (numeric) the mint's zerocoin denomination \n"
            "    \"p\": \"pubcoin\", (string) The public coin\n"
            "    \"s\": \"serial\",  (string) The secret serial number\n"
            "    \"r\": \"random\",  (string) The secret random number\n"
            "    \"t\": \"txid\",    (string) The txid that the coin was minted in\n"
            "    \"h\": n,         (numeric) The height the tx was added to the blockchain\n"
            "    \"u\": used,      (boolean) Whether the mint has been spent\n"
            "    \"v\": version,   (numeric) The version of the zPIV\n"
            "    \"k\": \"privkey\"  (string) The zPIV private key (V2+ zPIV only)\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("exportzerocoins", "false 5") + HelpExampleRpc("exportzerocoins", "false 5"));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    CWalletDB walletdb(pwalletMain->strWalletFile);

    bool fIncludeSpent = request.params[0].get_bool();
    libzerocoin::CoinDenomination denomination = libzerocoin::ZQ_ERROR;
    if (request.params.size() == 2)
        denomination = libzerocoin::IntToZerocoinDenomination(request.params[1].get_int());

    CzPIVTracker* zpivTracker = pwalletMain->zpivTracker.get();
    std::set<CMintMeta> setMints = zpivTracker->ListMints(!fIncludeSpent, false, false);

    UniValue jsonList(UniValue::VARR);
    for (const CMintMeta& meta : setMints) {
        if (denomination != libzerocoin::ZQ_ERROR && denomination != meta.denom)
            continue;

        CZerocoinMint mint;
        if (!pwalletMain->GetMint(meta.hashSerial, mint))
            continue;

        UniValue objMint(UniValue::VOBJ);
        objMint.pushKV("id", meta.hashSerial.GetHex());
        objMint.pushKV("d", mint.GetDenomination());
        objMint.pushKV("p", mint.GetValue().GetHex());
        objMint.pushKV("s", mint.GetSerialNumber().GetHex());
        objMint.pushKV("r", mint.GetRandomness().GetHex());
        objMint.pushKV("t", mint.GetTxHash().GetHex());
        objMint.pushKV("h", mint.GetHeight());
        objMint.pushKV("u", mint.IsUsed());
        objMint.pushKV("v", mint.GetVersion());
        if (mint.GetVersion() >= 2) {
            CKey key;
            key.SetPrivKey(mint.GetPrivKey(), true);
            objMint.pushKV("k", EncodeSecret(key));
        }
        jsonList.push_back(objMint);
    }

    return jsonList;
}

UniValue importzerocoins(const JSONRPCRequest& request)
{
    if(request.fHelp || request.params.size() == 0)
        throw std::runtime_error(
            "importzerocoins importdata \n"
            "\n[{\"d\":denomination,\"p\":\"pubcoin_hex\",\"s\":\"serial_hex\",\"r\":\"randomness_hex\",\"t\":\"txid\",\"h\":height, \"u\":used},{\"d\":...}]\n"
            "\nImport zerocoin mints.\n"
            "Adds raw zerocoin mints to the wallet.\n"
            "Note it is recommended to use the json export created from the exportzerocoins RPC call\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"importdata\"    (string, required) A json array of json objects containing zerocoin mints\n"

            "\nResult:\n"
            "{\n"
            "  \"added\": n,        (numeric) The quantity of zerocoin mints that were added\n"
            "  \"value\": amount    (numeric) The total zPIV value of zerocoin mints that were added\n"
            "}\n"

            "\nExamples\n" +
            HelpExampleCli("importzerocoins", "\'[{\"d\":100,\"p\":\"mypubcoin\",\"s\":\"myserial\",\"r\":\"randomness_hex\",\"t\":\"mytxid\",\"h\":104923, \"u\":false},{\"d\":5,...}]\'") +
            HelpExampleRpc("importzerocoins", "[{\"d\":100,\"p\":\"mypubcoin\",\"s\":\"myserial\",\"r\":\"randomness_hex\",\"t\":\"mytxid\",\"h\":104923, \"u\":false},{\"d\":5,...}]"));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    RPCTypeCheck(request.params, boost::assign::list_of(UniValue::VARR)(UniValue::VOBJ));
    UniValue arrMints = request.params[0].get_array();
    CWalletDB walletdb(pwalletMain->strWalletFile);

    int count = 0;
    CAmount nValue = 0;
    for (unsigned int idx = 0; idx < arrMints.size(); idx++) {
        const UniValue &val = arrMints[idx];
        const UniValue &o = val.get_obj();

        const UniValue& vDenom = find_value(o, "d");
        if (!vDenom.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing d key");
        int d = vDenom.get_int();
        if (d < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, d must be positive");

        libzerocoin::CoinDenomination denom = libzerocoin::IntToZerocoinDenomination(d);
        CBigNum bnValue = 0;
        bnValue.SetHex(find_value(o, "p").get_str());
        CBigNum bnSerial = 0;
        bnSerial.SetHex(find_value(o, "s").get_str());
        CBigNum bnRandom = 0;
        bnRandom.SetHex(find_value(o, "r").get_str());
        uint256 txid(uint256S(find_value(o, "t").get_str()));

        int nHeight = find_value(o, "h").get_int();
        if (nHeight < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, h must be positive");

        bool fUsed = find_value(o, "u").get_bool();

        //Assume coin is version 1 unless it has the version actually set
        uint8_t nVersion = 1;
        const UniValue& vVersion = find_value(o, "v");
        if (vVersion.isNum())
            nVersion = static_cast<uint8_t>(vVersion.get_int());

        //Set the privkey if applicable
        CPrivKey privkey;
        if (nVersion >= libzerocoin::PrivateCoin::PUBKEY_VERSION) {
            std::string strPrivkey = find_value(o, "k").get_str();
            CKey key = DecodeSecret(strPrivkey);
            if (!key.IsValid())
                return JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "privkey is not valid");
            privkey = key.GetPrivKey();
        }

        CZerocoinMint mint(denom, bnValue, bnRandom, bnSerial, fUsed, nVersion, &privkey);
        mint.SetTxHash(txid);
        mint.SetHeight(nHeight);
        pwalletMain->zpivTracker->Add(mint, true);
        count++;
        nValue += libzerocoin::ZerocoinDenominationToAmount(denom);
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("added", count);
    ret.pushKV("value", ValueFromAmount(nValue));
    return ret;
}

UniValue reconsiderzerocoins(const JSONRPCRequest& request)
{
    if(request.fHelp || !request.params.empty())
        throw std::runtime_error(
            "reconsiderzerocoins\n"
            "\nCheck archived zPIV list to see if any mints were added to the blockchain.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txid\" : \"xxx\",           (string) the mint's zerocoin denomination \n"
            "    \"denomination\" : amount,  (numeric) the mint's zerocoin denomination\n"
            "    \"pubcoin\" : \"xxx\",        (string) The mint's public identifier\n"
            "    \"height\" : n              (numeric) The height the tx was added to the blockchain\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples\n" +
            HelpExampleCli("reconsiderzerocoins", "") + HelpExampleRpc("reconsiderzerocoins", ""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked(true);

    std::list<CZerocoinMint> listMints;
    std::list<CDeterministicMint> listDMints;
    pwalletMain->ReconsiderZerocoins(listMints, listDMints);

    UniValue arrRet(UniValue::VARR);
    for (const CZerocoinMint& mint : listMints) {
        UniValue objMint(UniValue::VOBJ);
        objMint.pushKV("txid", mint.GetTxHash().GetHex());
        objMint.pushKV("denomination", ValueFromAmount(mint.GetDenominationAsAmount()));
        objMint.pushKV("pubcoin", mint.GetValue().GetHex());
        objMint.pushKV("height", mint.GetHeight());
        arrRet.push_back(objMint);
    }
    for (const CDeterministicMint& dMint : listDMints) {
        UniValue objMint(UniValue::VOBJ);
        objMint.pushKV("txid", dMint.GetTxHash().GetHex());
        objMint.pushKV("denomination", FormatMoney(libzerocoin::ZerocoinDenominationToAmount(dMint.GetDenomination())));
        objMint.pushKV("pubcoinhash", dMint.GetPubcoinHash().GetHex());
        objMint.pushKV("height", dMint.GetHeight());
        arrRet.push_back(objMint);
    }

    return arrRet;
}

UniValue setzpivseed(const JSONRPCRequest& request)
{
    if(request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "setzpivseed \"seed\"\n"
            "\nSet the wallet's deterministic zpiv seed to a specific value.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"seed\"        (string, required) The deterministic zpiv seed.\n"

            "\nResult\n"
            "\"success\" : b,  (boolean) Whether the seed was successfully set.\n"

            "\nExamples\n" +
            HelpExampleCli("setzpivseed", "63f793e7895dd30d99187b35fbfb314a5f91af0add9e0a4e5877036d1e392dd5") +
            HelpExampleRpc("setzpivseed", "63f793e7895dd30d99187b35fbfb314a5f91af0add9e0a4e5877036d1e392dd5"));

    EnsureWalletIsUnlocked();

    uint256 seed;
    seed.SetHex(request.params[0].get_str());

    CzPIVWallet* zwallet = pwalletMain->getZWallet();
    bool fSuccess = zwallet->SetMasterSeed(seed, true);
    if (fSuccess)
        zwallet->SyncWithChain();

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("success", fSuccess);

    return ret;
}

UniValue getzpivseed(const JSONRPCRequest& request)
{
    if(request.fHelp || !request.params.empty())
        throw std::runtime_error(
            "getzpivseed\n"
            "\nCheck archived zPIV list to see if any mints were added to the blockchain.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nResult\n"
            "\"seed\" : s,  (string) The deterministic zPIV seed.\n"

            "\nExamples\n" +
            HelpExampleCli("getzpivseed", "") + HelpExampleRpc("getzpivseed", ""));

    EnsureWalletIsUnlocked();

    CzPIVWallet* zwallet = pwalletMain->getZWallet();
    uint256 seed = zwallet->GetMasterSeed();

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("seed", seed.GetHex());

    return ret;
}

UniValue generatemintlist(const JSONRPCRequest& request)
{
    if(request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "generatemintlist\n"
            "\nShow mints that are derived from the deterministic zPIV seed.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments\n"
            "1. \"count\"  : n,  (numeric) Which sequential zPIV to start with.\n"
            "2. \"range\"  : n,  (numeric) How many zPIV to generate.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"count\": n,          (numeric) Deterministic Count.\n"
            "    \"value\": \"xxx\",    (string) Hex encoded pubcoin value.\n"
            "    \"randomness\": \"xxx\",   (string) Hex encoded randomness.\n"
            "    \"serial\": \"xxx\"        (string) Hex encoded Serial.\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples\n" +
            HelpExampleCli("generatemintlist", "1, 100") + HelpExampleRpc("generatemintlist", "1, 100"));

    EnsureWalletIsUnlocked();

    int nCount = request.params[0].get_int();
    int nRange = request.params[1].get_int();
    CzPIVWallet* zwallet = pwalletMain->getZWallet();

    UniValue arrRet(UniValue::VARR);
    for (int i = nCount; i < nCount + nRange; i++) {
        libzerocoin::CoinDenomination denom = libzerocoin::ZQ_ONE;
        libzerocoin::PrivateCoin coin(Params().GetConsensus().Zerocoin_Params(false), denom, false);
        CDeterministicMint dMint;
        zwallet->GenerateMint(i, denom, coin, dMint);
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("count", i);
        obj.pushKV("value", coin.getPublicCoin().getValue().GetHex());
        obj.pushKV("randomness", coin.getRandomness().GetHex());
        obj.pushKV("serial", coin.getSerialNumber().GetHex());
        arrRet.push_back(obj);
    }

    return arrRet;
}

UniValue dzpivstate(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
                "dzpivstate\n"
                        "\nThe current state of the mintpool of the deterministic zPIV wallet.\n" +
                HelpRequiringPassphrase() + "\n"

                        "\nExamples\n" +
                HelpExampleCli("mintpoolstatus", "") + HelpExampleRpc("mintpoolstatus", ""));

    CzPIVWallet* zwallet = pwalletMain->getZWallet();
    UniValue obj(UniValue::VOBJ);
    int nCount, nCountLastUsed;
    zwallet->GetState(nCount, nCountLastUsed);
    obj.pushKV("dzpiv_count", nCount);
    obj.pushKV("mintpool_count", nCountLastUsed);

    return obj;
}

void static SearchThread(CzPIVWallet* zwallet, int nCountStart, int nCountEnd)
{
    LogPrintf("%s: start=%d end=%d\n", __func__, nCountStart, nCountEnd);
    CWalletDB walletDB(pwalletMain->strWalletFile);
    try {
        uint256 seedMaster = zwallet->GetMasterSeed();
        uint256 hashSeed = Hash(seedMaster.begin(), seedMaster.end());
        for(int i = nCountStart; i < nCountEnd; i++) {
            boost::this_thread::interruption_point();
            CDataStream ss(SER_GETHASH, 0);
            ss << seedMaster << i;
            uint512 zerocoinSeed = Hash512(ss.begin(), ss.end());

            CBigNum bnValue;
            CBigNum bnSerial;
            CBigNum bnRandomness;
            CKey key;
            zwallet->SeedToZPIV(zerocoinSeed, bnValue, bnSerial, bnRandomness, key);

            uint256 hashPubcoin = GetPubCoinHash(bnValue);
            zwallet->AddToMintPool(std::make_pair(hashPubcoin, i), true);
            walletDB.WriteMintPoolPair(hashSeed, hashPubcoin, i);
        }
    } catch (const std::exception& e) {
        LogPrintf("SearchThread() exception");
    } catch (...) {
        LogPrintf("SearchThread() exception");
    }
}

UniValue searchdzpiv(const JSONRPCRequest& request)
{
    if(request.fHelp || request.params.size() != 3)
        throw std::runtime_error(
            "searchdzpiv\n"
            "\nMake an extended search for deterministically generated zPIV that have not yet been recognized by the wallet.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments\n"
            "1. \"count\"       (numeric) Which sequential zPIV to start with.\n"
            "2. \"range\"       (numeric) How many zPIV to generate.\n"
            "3. \"threads\"     (numeric) How many threads should this operation consume.\n"

            "\nExamples\n" +
            HelpExampleCli("searchdzpiv", "1, 100, 2") + HelpExampleRpc("searchdzpiv", "1, 100, 2"));

    EnsureWalletIsUnlocked();

    int nCount = request.params[0].get_int();
    if (nCount < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Count cannot be less than 0");

    int nRange = request.params[1].get_int();
    if (nRange < 1)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Range has to be at least 1");

    int nThreads = request.params[2].get_int();

    CzPIVWallet* zwallet = pwalletMain->getZWallet();

    boost::thread_group* dzpivThreads = new boost::thread_group();
    int nRangePerThread = nRange / nThreads;

    int nPrevThreadEnd = nCount - 1;
    for (int i = 0; i < nThreads; i++) {
        int nStart = nPrevThreadEnd + 1;;
        int nEnd = nStart + nRangePerThread;
        nPrevThreadEnd = nEnd;
        dzpivThreads->create_thread(boost::bind(&SearchThread, zwallet, nStart, nEnd));
    }

    dzpivThreads->join_all();

    zwallet->RemoveMintsFromPool(pwalletMain->zpivTracker->GetSerialHashes());
    zwallet->SyncWithChain(false);

    //todo: better response
    return "done";
}

UniValue spendrawzerocoin(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 4 || request.params.size() > 6)
        throw std::runtime_error(
            "spendrawzerocoin \"serialHex\" denom \"randomnessHex\" \"priv key\" ( \"address\" \"mintTxId\" )\n"
            "\nCreate and broadcast a TX spending the provided zericoin.\n"

            "\nArguments:\n"
            "1. \"serialHex\"        (string, required) A zerocoin serial number (hex)\n"
            "2. \"randomnessHex\"    (string, required) A zerocoin randomness value (hex)\n"
            "3. denom                (numeric, required) A zerocoin denomination (decimal)\n"
            "4. \"priv key\"         (string, required) The private key associated with this coin (hex)\n"
            "5. \"address\"          (string, optional) PIVX address to spend to. If not specified, "
            "                        or empty string, spend to change address.\n"
            "6. \"mintTxId\"         (string, optional) txid of the transaction containing the mint. If not"
            "                        specified, or empty string, the blockchain will be scanned (could take a while)"

            "\nResult:\n"
                "\"txid\"             (string) The transaction txid in hex\n"

            "\nExamples\n" +
            HelpExampleCli("spendrawzerocoin", "\"f80892e78c30a393ef4ab4d5a9d5a2989de6ebc7b976b241948c7f489ad716a2\" \"a4fd4d7248e6a51f1d877ddd2a4965996154acc6b8de5aa6c83d4775b283b600\" 100 \"xxx\"") +
            HelpExampleRpc("spendrawzerocoin", "\"f80892e78c30a393ef4ab4d5a9d5a2989de6ebc7b976b241948c7f489ad716a2\", \"a4fd4d7248e6a51f1d877ddd2a4965996154acc6b8de5aa6c83d4775b283b600\", 100, \"xxx\""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (sporkManager.IsSporkActive(SPORK_16_ZEROCOIN_MAINTENANCE_MODE))
            throw JSONRPCError(RPC_WALLET_ERROR, "zPIV is currently disabled due to maintenance.");

    const Consensus::Params& consensus = Params().GetConsensus();

    CBigNum serial;
    serial.SetHex(request.params[0].get_str());

    CBigNum randomness;
    randomness.SetHex(request.params[1].get_str());

    const int denom_int = request.params[2].get_int();
    libzerocoin::CoinDenomination denom = libzerocoin::IntToZerocoinDenomination(denom_int);

    std::string priv_key_str = request.params[3].get_str();
    CPrivKey privkey;
    CKey key = DecodeSecret(priv_key_str);
    if (!key.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "privkey is not valid");
    privkey = key.GetPrivKey();

    // Create the coin associated with these secrets
    libzerocoin::PrivateCoin coin(consensus.Zerocoin_Params(false), denom, serial, randomness);
    coin.setPrivKey(privkey);
    coin.setVersion(libzerocoin::PrivateCoin::CURRENT_VERSION);

    // Create the mint associated with this coin
    CZerocoinMint mint(denom, coin.getPublicCoin().getValue(), randomness, serial, false, CZerocoinMint::CURRENT_VERSION, &privkey);

    std::string address_str = "";
    if (request.params.size() > 4)
        address_str = request.params[4].get_str();

    if (request.params.size() > 5) {
        // update mint txid
        mint.SetTxHash(ParseHashV(request.params[5], "parameter 5"));
    } else {
        // If the mint tx is not provided, look for it
        const CBigNum& mintValue = mint.GetValue();
        bool found = false;
        {
            CBlockIndex* pindex = chainActive.Tip();
            while (!found && pindex && consensus.NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_ZC)) {
                LogPrintf("%s : Checking block %d...\n", __func__, pindex->nHeight);
                CBlock block;
                if (!ReadBlockFromDisk(block, pindex))
                    throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to read block from disk");
                std::list<CZerocoinMint> listMints;
                BlockToZerocoinMintList(block, listMints, true);
                for (const CZerocoinMint& m : listMints) {
                    if (m.GetValue() == mintValue && m.GetDenomination() == denom) {
                        // mint found. update txid
                        mint.SetTxHash(m.GetTxHash());
                        found = true;
                        break;
                    }
                }
                pindex = pindex->pprev;
            }
        }
        if (!found)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Mint tx not found");
    }

    std::vector<CZerocoinMint> vMintsSelected = {mint};
    return DoZpivSpend(mint.GetDenominationAsAmount(), vMintsSelected, address_str);
}

static const CRPCCommand commands[] =
{ //  category              name                        actor (function)           okSafeMode
  //  --------------------- ------------------------    -----------------------    ----------
    { "zerocoin",           "getzerocoinbalance",       &getzerocoinbalance,       false },
    { "zerocoin",           "listmintedzerocoins",      &listmintedzerocoins,      false },
    { "zerocoin",           "listspentzerocoins",       &listspentzerocoins,       false },
    { "zerocoin",           "listzerocoinamounts",      &listzerocoinamounts,      false },
    { "zerocoin",           "spendzerocoin",            &spendzerocoin,            false },
    { "zerocoin",           "spendrawzerocoin",         &spendrawzerocoin,         true  },
    { "zerocoin",           "spendzerocoinmints",       &spendzerocoinmints,       false },
    { "zerocoin",           "resetmintzerocoin",        &resetmintzerocoin,        false },
    { "zerocoin",           "resetspentzerocoin",       &resetspentzerocoin,       false },
    { "zerocoin",           "getarchivedzerocoin",      &getarchivedzerocoin,      false },
    { "zerocoin",           "importzerocoins",          &importzerocoins,          false },
    { "zerocoin",           "exportzerocoins",          &exportzerocoins,          false },
    { "zerocoin",           "reconsiderzerocoins",      &reconsiderzerocoins,      false },
    { "zerocoin",           "getzpivseed",              &getzpivseed,              false },
    { "zerocoin",           "setzpivseed",              &setzpivseed,              false },
    { "zerocoin",           "generatemintlist",         &generatemintlist,         false },
    { "zerocoin",           "searchdzpiv",              &searchdzpiv,              false },
    { "zerocoin",           "dzpivstate",               &dzpivstate,               false },

    /* Not shown in help */
    { "hidden",             "mintzerocoin",             &mintzerocoin,             false },
};

void RegisterZPIVRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
