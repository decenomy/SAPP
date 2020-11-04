// Copyright (c) 2020 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_SAPLING_OPERATION_H
#define PIVX_SAPLING_OPERATION_H

#include "amount.h"
#include "sapling/transaction_builder.h"
#include "primitives/transaction.h"
#include "wallet/wallet.h"

struct TxValues;

class SendManyRecipient {
public:
    const std::string address;
    const CAmount amount;
    const std::string memo;

    SendManyRecipient(const std::string& address_, CAmount amount_, std::string memo_) :
            address(address_), amount(amount_), memo(memo_) {}
};

class FromAddress {
public:
    explicit FromAddress() {};
    explicit FromAddress(const CTxDestination& _fromTaddr) : fromTaddr(_fromTaddr) {};
    explicit FromAddress(const libzcash::SaplingPaymentAddress& _fromSapaddr) : fromSapAddr(_fromSapaddr) {};

    bool isFromTAddress() const { return IsValidDestination(fromTaddr); }
    bool isFromSapAddress() const { return fromSapAddr.is_initialized(); }

    CTxDestination fromTaddr{CNoDestination()};
    Optional<libzcash::SaplingPaymentAddress> fromSapAddr{nullopt};
};

class OperationResult {
public:
    explicit OperationResult(bool result, const std::string& error = "") : m_result(result), m_error(error)  {}
    bool m_result;
    std::string m_error;

    explicit operator bool() const { return m_result; }
};

class SaplingOperation {
public:
    explicit SaplingOperation(const Consensus::Params& consensusParams, int chainHeight) : txBuilder(consensusParams, chainHeight) {};
    explicit SaplingOperation(TransactionBuilder& _builder) : txBuilder(_builder) {};

    OperationResult send(std::string& retTxHash);

    void setFromAddress(const CTxDestination&);
    void setFromAddress(const libzcash::SaplingPaymentAddress&);
    SaplingOperation* setTransparentRecipients(std::vector<SendManyRecipient>& vec) { taddrRecipients = std::move(vec); return this; };
    SaplingOperation* setShieldedRecipients(std::vector<SendManyRecipient>& vec) { shieldedAddrRecipients = std::move(vec); return this; } ;
    SaplingOperation* setFee(CAmount _fee) { fee = _fee; return this; }
    SaplingOperation* setMinDepth(int _mindepth) { assert(_mindepth >= 0); mindepth = _mindepth; return this; }
    SaplingOperation* setTxBuilder(TransactionBuilder& builder) { txBuilder = builder; return this; }

    CTransaction getFinalTx() { return finalTx; }

    // Public only for unit test coverage
    bool getMemoFromHexString(const std::string& s, std::array<unsigned char, ZC_MEMO_SIZE> memoRet, std::string& error);

    // Test only
    bool testMode{false};

private:
    FromAddress fromAddress;
    std::vector<SendManyRecipient> taddrRecipients;
    std::vector<SendManyRecipient> shieldedAddrRecipients;
    std::vector<COutput> transInputs;
    std::vector<SaplingNoteEntry> shieldedInputs;
    int mindepth{5}; // Min default depth 5.
    CAmount fee{0};

    // Builder
    TransactionBuilder txBuilder;
    CTransaction finalTx;

    OperationResult loadUtxos(TxValues& values);
    OperationResult loadUnspentNotes(TxValues& txValues, const libzcash::SaplingExpandedSpendingKey& expsk);
    OperationResult checkTxValues(TxValues& txValues, bool isFromtAddress, bool isFromShielded);
};

#endif //PIVX_SAPLING_OPERATION_H
