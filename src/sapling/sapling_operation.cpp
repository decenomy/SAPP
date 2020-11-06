// Copyright (c) 2020 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "sapling/sapling_operation.h"

#include "net.h" // for g_connman
#include "policy/policy.h" // for GetDustThreshold
#include "sapling/key_io_sapling.h"
#include "utilmoneystr.h"        // for FormatMoney

struct TxValues
{
    CAmount transInTotal{0};
    CAmount shieldedInTotal{0};
    CAmount transOutTotal{0};
    CAmount shieldedOutTotal{0};
    CAmount target{0};
};

OperationResult SaplingOperation::checkTxValues(TxValues& txValues, bool isFromtAddress, bool isFromShielded)
{
    assert(!isFromtAddress || txValues.shieldedInTotal == 0);
    assert(!isFromShielded || txValues.transInTotal == 0);

    if (isFromtAddress && (txValues.transInTotal < txValues.target)) {
        return errorOut(strprintf("Insufficient transparent funds, have %s, need %s",
                                  FormatMoney(txValues.transInTotal), FormatMoney(txValues.target)));
    }

    if (isFromShielded && (txValues.shieldedInTotal < txValues.target)) {
        return errorOut(strprintf("Insufficient shielded funds, have %s, need %s",
                                  FormatMoney(txValues.shieldedInTotal), FormatMoney(txValues.target)));
    }
    return OperationResult(true);
}

OperationResult SaplingOperation::send(std::string& retTxHash)
{

    bool isFromtAddress = fromAddress.isFromTAddress();
    bool isFromShielded = fromAddress.isFromSapAddress();

    // It needs to have a from (for now at least)
    if (!isFromtAddress && !isFromShielded) {
        return errorOut("From address parameter missing");
    }

    if (taddrRecipients.empty() && shieldedAddrRecipients.empty()) {
        return errorOut("No recipients");
    }

    if (isFromShielded && mindepth == 0) {
        return errorOut("Minconf cannot be zero when sending from shielded address");
    }

    // Get necessary keys
    libzcash::SaplingExpandedSpendingKey expsk;
    uint256 ovk;
    if (isFromShielded) {
        // Get spending key for address
        libzcash::SaplingExtendedSpendingKey sk;
        if (!pwalletMain->GetSaplingExtendedSpendingKey(fromAddress.fromSapAddr.get(), sk)) {
            return errorOut("Spending key not in the wallet");
        }
        expsk = sk.expsk;
        ovk = expsk.full_viewing_key().ovk;
    } else {
        // Sending from a t-address, which we don't have an ovk for. Instead,
        // generate a common one from the HD seed. This ensures the data is
        // recoverable, while keeping it logically separate from the ZIP 32
        // Sapling key hierarchy, which the user might not be using.
        ovk = pwalletMain->GetSaplingScriptPubKeyMan()->getCommonOVKFromSeed();
    }

    // Results
    TxValues txValues;
    // Add transparent outputs
    for (SendManyRecipient &t : taddrRecipients) {
        txValues.transOutTotal += t.amount;
        txBuilder.AddTransparentOutput(DecodeDestination(t.address), t.amount);
    }

    // Add shielded outputs
    for (const SendManyRecipient &t : shieldedAddrRecipients) {
        txValues.shieldedOutTotal += t.amount;
        auto addr = KeyIO::DecodePaymentAddress(t.address);
        assert(IsValidPaymentAddress(addr));
        auto to = boost::get<libzcash::SaplingPaymentAddress>(addr);
        std::array<unsigned char, ZC_MEMO_SIZE> memo = {};
        std::string error;
        if (!getMemoFromHexString(t.memo, memo, error))
            return errorOut(error);
        txBuilder.AddSaplingOutput(ovk, to, t.amount, memo);
    }

    // Load total
    txValues.target = txValues.shieldedOutTotal + txValues.transOutTotal + fee;
    OperationResult result(false);

    // If from address is a taddr, select UTXOs to spend
    // note: when spending coinbase utxos, you can only specify a single shielded addr as the change must go somewhere
    // and if there are multiple shielded addrs, we don't know where to send it.
    if (isFromtAddress && !(result = loadUtxos(txValues))) {
        return result;
    }

    // If from a shielded addr, select notes to spend
    if (isFromShielded) {
        // Load notes
        if (!(result = loadUnspentNotes(txValues, expsk))) {
            return result;
        }
    }

    const auto& retCalc = checkTxValues(txValues, isFromtAddress, isFromShielded);
    if (!retCalc) return retCalc;

    LogPrint(BCLog::SAPLING, "%s: spending %s to send %s with fee %s\n", __func__ , FormatMoney(txValues.target), FormatMoney(txValues.shieldedOutTotal + txValues.transOutTotal), FormatMoney(fee));
    LogPrint(BCLog::SAPLING, "%s: transparent input: %s (to choose from)\n", __func__ , FormatMoney(txValues.transInTotal));
    LogPrint(BCLog::SAPLING, "%s: private input: %s (to choose from)\n", __func__ , FormatMoney(txValues.shieldedInTotal));
    LogPrint(BCLog::SAPLING, "%s: transparent output: %s\n", __func__ , FormatMoney(txValues.transOutTotal));
    LogPrint(BCLog::SAPLING, "%s: private output: %s\n", __func__ , FormatMoney(txValues.shieldedOutTotal));
    LogPrint(BCLog::SAPLING, "%s: fee: %s\n", __func__ , FormatMoney(fee));

    // Set change address if we are using transparent funds
    CReserveKey keyChange(pwalletMain);
    if (isFromtAddress) {
        CPubKey vchPubKey;
        if (!keyChange.GetReservedKey(vchPubKey, true)) {
            // should never fail, as we just unlocked
            return errorOut("Could not generate a taddr to use as a change address");
        }

        CTxDestination changeAddr = vchPubKey.GetID();
        txBuilder.SendChangeTo(changeAddr);
    }

    // Build the transaction
    txBuilder.SetFee(fee);
    finalTx = txBuilder.Build().GetTxOrThrow();

    if (!testMode) {
        CWalletTx wtx(pwalletMain, finalTx);
        const CWallet::CommitResult& res = pwalletMain->CommitTransaction(wtx, keyChange, g_connman.get());
        if (res.status != CWallet::CommitStatus::OK) {
            return errorOut(res.ToString());
        }
    }

    retTxHash = finalTx.GetHash().ToString();
    return OperationResult(true);
}

void SaplingOperation::setFromAddress(const CTxDestination& _dest)
{
    fromAddress = FromAddress(_dest);
}

void SaplingOperation::setFromAddress(const libzcash::SaplingPaymentAddress& _payment)
{
    fromAddress = FromAddress(_payment);
}

OperationResult SaplingOperation::loadUtxos(TxValues& txValues)
{
    std::set<CTxDestination> destinations;
    destinations.insert(fromAddress.fromTaddr);
    if (!pwalletMain->AvailableCoins(
            &transInputs,
            nullptr,
            false,
            false,
            ALL_COINS,
            true,
            true,
            &destinations,
            mindepth)) {
        return errorOut("Insufficient funds, no available UTXO to spend");
    }

    // sort in ascending order, so smaller utxos appear first
    std::sort(transInputs.begin(), transInputs.end(), [](const COutput& i, const COutput& j) -> bool {
        return i.Value() < j.Value();
    });

    // Final step, append utxo to the transaction

    // Get dust threshold
    CKey secret;
    secret.MakeNewKey(true);
    CScript scriptPubKey = GetScriptForDestination(secret.GetPubKey().GetID());
    CTxOut out(CAmount(1), scriptPubKey);
    CAmount dustThreshold = GetDustThreshold(out, minRelayTxFee);
    CAmount dustChange = -1;

    CAmount selectedUTXOAmount = 0;
    std::vector<COutput> selectedTInputs;
    for (const COutput& t : transInputs) {
        const auto& outPoint = t.tx->vout[t.i];
        selectedUTXOAmount += outPoint.nValue;
        selectedTInputs.emplace_back(t);
        if (selectedUTXOAmount >= txValues.target) {
            // Select another utxo if there is change less than the dust threshold.
            dustChange = selectedUTXOAmount - txValues.target;
            if (dustChange == 0 || dustChange >= dustThreshold) {
                break;
            }
        }
    }

    // If there is transparent change, is it valid or is it dust?
    if (dustChange < dustThreshold && dustChange != 0) {
        return errorOut(strprintf("Insufficient transparent funds, have %s, need %s more to avoid creating invalid change output %s (dust threshold is %s)",
                                  FormatMoney(txValues.transInTotal), FormatMoney(dustThreshold - dustChange), FormatMoney(dustChange), FormatMoney(dustThreshold)));
    }

    transInputs = selectedTInputs;
    txValues.transInTotal = selectedUTXOAmount;

    // update the transaction with these inputs
    for (const auto& t : transInputs) {
        const auto& outPoint = t.tx->vout[t.i];
        txBuilder.AddTransparentInput(COutPoint(t.tx->GetHash(), t.i), outPoint.scriptPubKey, outPoint.nValue);
    }

    return OperationResult(true);
}

OperationResult SaplingOperation::loadUnspentNotes(TxValues& txValues, const libzcash::SaplingExpandedSpendingKey& expsk)
{
    std::vector<SaplingNoteEntry> saplingEntries;
    libzcash::PaymentAddress paymentAddress(fromAddress.fromSapAddr.get());
    pwalletMain->GetSaplingScriptPubKeyMan()->GetFilteredNotes(saplingEntries, paymentAddress, mindepth);

    for (const auto& entry : saplingEntries) {
        shieldedInputs.emplace_back(entry);
        std::string data(entry.memo.begin(), entry.memo.end());
        LogPrint(BCLog::SAPLING,"%s: found unspent Sapling note (txid=%s, vShieldedSpend=%d, amount=%s, memo=%s)\n",
                 __func__ ,
                 entry.op.hash.ToString().substr(0, 10),
                 entry.op.n,
                 FormatMoney(entry.note.value()),
                 HexStr(data).substr(0, 10));
    }

    if (shieldedInputs.empty()) {
        return errorOut("Insufficient funds, no available notes to spend");
    }

    // sort in descending order, so big notes appear first
    std::sort(shieldedInputs.begin(), shieldedInputs.end(),
              [](const SaplingNoteEntry& i, const SaplingNoteEntry& j) -> bool {
                  return i.note.value() > j.note.value();
              });

    // Now select the notes that we are going to use.
    std::vector<SaplingOutPoint> ops;
    std::vector<libzcash::SaplingNote> notes;
    CAmount sum = 0;
    for (const auto& t : shieldedInputs) {
        ops.emplace_back(t.op);
        notes.emplace_back(t.note);
        sum += t.note.value();
        txValues.shieldedInTotal += t.note.value();
        if (sum >= txValues.target) {
            break;
        }
    }

    // Fetch Sapling anchor and witnesses
    uint256 anchor;
    std::vector<boost::optional<SaplingWitness>> witnesses;
    pwalletMain->GetSaplingScriptPubKeyMan()->GetSaplingNoteWitnesses(ops, witnesses, anchor);

    // Add Sapling spends
    for (size_t i = 0; i < notes.size(); i++) {
        if (!witnesses[i]) {
            return errorOut("Missing witness for Sapling note");
        }
        txBuilder.AddSaplingSpend(expsk, notes[i], anchor, witnesses[i].get());
    }

    return OperationResult(true);
}

bool SaplingOperation::getMemoFromHexString(const std::string& s, std::array<unsigned char, ZC_MEMO_SIZE> memoRet, std::string& error) {
    // initialize to default memo (no_memo), see section 5.5 of the protocol spec
    std::array<unsigned char, ZC_MEMO_SIZE> memo = {{0xF6}};

    std::vector<unsigned char> rawMemo = ParseHex(s.c_str());

    // If ParseHex comes across a non-hex char, it will stop but still return results so far.
    size_t slen = s.length();
    if (slen % 2 !=0 || (slen>0 && rawMemo.size()!=slen/2)) {
        error = "Memo must be in hexadecimal format";
        return false;
    }

    if (rawMemo.size() > ZC_MEMO_SIZE) {
        error = strprintf("Memo size of %d is too big, maximum allowed is %d", rawMemo.size(), ZC_MEMO_SIZE);
        return false;
    }

    // copy vector into array
    int lenMemo = rawMemo.size();
    for (int i = 0; i < ZC_MEMO_SIZE && i < lenMemo; i++) {
        memo[i] = rawMemo[i];
    }

    memoRet = memo;
    return true;
}
