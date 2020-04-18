// Copyright (c) 2020 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_SAPLINGSCRIPTPUBKEYMAN_H
#define PIVX_SAPLINGSCRIPTPUBKEYMAN_H

#include "consensus/consensus.h"
#include "wallet/hdchain.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "sapling/incrementalmerkletree.hpp"

//! Size of witness cache
//  Should be large enough that we can expect not to reorg beyond our cache
//  unless there is some exceptional network disruption.
static const unsigned int WITNESS_CACHE_SIZE = DEFAULT_MAX_REORG_DEPTH + 1;

class CBlock;
class CBlockIndex;

class SaplingNoteData
{
public:

    SaplingNoteData() : nullifier() { }
    SaplingNoteData(const libzcash::SaplingIncomingViewingKey& _ivk) : ivk {_ivk}, nullifier() { }
    SaplingNoteData(const libzcash::SaplingIncomingViewingKey& _ivk, const uint256& n) : ivk {_ivk}, nullifier(n) { }

    std::list<SaplingWitness> witnesses;
    libzcash::SaplingIncomingViewingKey ivk;

    /**
     * Block height corresponding to the most current witness.
     *
     * When we first create a SaplingNoteData in SaplingScriptPubKeyMan::FindMySaplingNotes, this is set to
     * -1 as a placeholder. The next time CWallet::ChainTip is called, we can
     * determine what height the witness cache for this note is valid for (even
     * if no witnesses were cached), and so can set the correct value in
     * SaplingScriptPubKeyMan::IncrementNoteWitnesses and SaplingScriptPubKeyMan::DecrementNoteWitnesses.
     */
    int witnessHeight{-1};

    /**
     * Cached note nullifier. May not be set if the wallet was not unlocked when
     * this SaplingNoteData was created. If not set, we always assume that the
     * note has not been spent.
     *
     * It's okay to cache the nullifier in the wallet, because we are storing
     * the spending key there too, which could be used to derive this.
     * If the wallet is encrypted, this means that someone with access to the
     * locked wallet cannot spend notes, but can connect received notes to the
     * transactions they are spent in. This is the same security semantics as
     * for transparent addresses.
     */
    Optional<uint256> nullifier;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(nVersion);
        }
        READWRITE(ivk);
        READWRITE(nullifier);
        READWRITE(witnesses);
        READWRITE(witnessHeight);
    }

    friend bool operator==(const SaplingNoteData& a, const SaplingNoteData& b) {
        return (a.ivk == b.ivk && a.nullifier == b.nullifier && a.witnessHeight == b.witnessHeight);
    }

    friend bool operator!=(const SaplingNoteData& a, const SaplingNoteData& b) {
        return !(a == b);
    }
};

enum KeyAddResult {
    SpendingKeyExists,
    KeyAlreadyExists,
    KeyAdded,
    KeyNotAdded,
};

/*
 * Sapling keys manager
 * todo: add real description..
 */
class SaplingScriptPubKeyMan {

public:
    SaplingScriptPubKeyMan(CWallet *parent) : wallet(parent) {}

    ~SaplingScriptPubKeyMan() {};

    /**
     * pindex is the new tip being connected.
     */
    void IncrementNoteWitnesses(const CBlockIndex* pindex,
                                const CBlock* pblock,
                                SaplingMerkleTree& saplingTree);
    /**
     * pindex is the old tip being disconnected.
     */
    void DecrementNoteWitnesses(const CBlockIndex* pindex);

    /**
     * Set and initialize the Sapling HD chain.
     */
    bool SetupGeneration(const CKeyID& keyID, bool force = false);
    bool IsEnabled() const { return !hdChain.IsNull(); };

    /* Set the current HD seed (will reset the chain child index counters)
      Sets the seed's version based on the current wallet version (so the
      caller must ensure the current wallet version is correct before calling
      this function). */
    void SetHDSeed(const CPubKey& key, bool force = false, bool memonly = false);
    void SetHDSeed(const CKeyID& keyID, bool force = false, bool memonly = false);

    /* Set the HD chain model (chain child index counters) */
    void SetHDChain(CHDChain& chain, bool memonly);
    const CHDChain& GetHDChain() const { return hdChain; }

    /* Encrypt Sapling keys */
    bool EncryptSaplingKeys(CKeyingMaterial& vMasterKeyIn);

    // Add full viewing key if it's not already in the wallet
    KeyAddResult AddViewingKeyToWallet(const libzcash::SaplingExtendedFullViewingKey &extfvk) const;

    // Add spending key if it's not already in the wallet
    KeyAddResult AddSpendingKeyToWallet(
            const Consensus::Params &params,
            const libzcash::SaplingExtendedSpendingKey &sk,
            int64_t nTime);

    //! Generates new Sapling key
    libzcash::SaplingPaymentAddress GenerateNewSaplingZKey();
    //! Adds Sapling spending key to the store, and saves it to disk
    bool AddSaplingZKey(const libzcash::SaplingExtendedSpendingKey &key);
    bool AddSaplingIncomingViewingKey(
            const libzcash::SaplingIncomingViewingKey &ivk,
            const libzcash::SaplingPaymentAddress &addr);
    bool AddCryptedSaplingSpendingKeyDB(
            const libzcash::SaplingExtendedFullViewingKey &extfvk,
            const std::vector<unsigned char> &vchCryptedSecret);
    //! Returns true if the wallet contains the spending key
    bool HaveSpendingKeyForPaymentAddress(const libzcash::SaplingPaymentAddress &zaddr) const;

    //! Adds spending key to the store, without saving it to disk (used by LoadWallet)
    bool LoadSaplingZKey(const libzcash::SaplingExtendedSpendingKey &key);
    //! Load spending key metadata (used by LoadWallet)
    bool LoadSaplingZKeyMetadata(const libzcash::SaplingIncomingViewingKey &ivk, const CKeyMetadata &meta);
    //! Adds an encrypted spending key to the store, without saving it to disk (used by LoadWallet)
    bool LoadCryptedSaplingZKey(const libzcash::SaplingExtendedFullViewingKey &extfvk,
                                const std::vector<unsigned char> &vchCryptedSecret);
    //! Adds a Sapling payment address -> incoming viewing key map entry,
    //! without saving it to disk (used by LoadWallet)
    bool LoadSaplingPaymentAddress(
            const libzcash::SaplingPaymentAddress &addr,
            const libzcash::SaplingIncomingViewingKey &ivk);
    bool AddSaplingSpendingKey(const libzcash::SaplingExtendedSpendingKey &sk);

    //! Return the full viewing key for the shielded address
    Optional<libzcash::SaplingExtendedFullViewingKey> GetViewingKeyForPaymentAddress(
            const libzcash::SaplingPaymentAddress &addr) const;

    //! Return the spending key for the payment address (nullopt if the wallet has no spending key for such address)
    Optional<libzcash::SaplingExtendedSpendingKey> GetSpendingKeyForPaymentAddress(const libzcash::SaplingPaymentAddress &addr) const;

    //! Clear every notesData from every wallet tx and reset the witness cache size
    void ClearNoteWitnessCache();

    // Sapling metadata
    std::map<libzcash::SaplingIncomingViewingKey, CKeyMetadata> mapSaplingZKeyMetadata;

    /*
     * Size of the incremental witness cache for the notes in our wallet.
     * This will always be greater than or equal to the size of the largest
     * incremental witness cache in any transaction in mapWallet.
     */
    int64_t nWitnessCacheSize{0};
    bool nWitnessCacheNeedsUpdate{false};

private:
    /* Parent wallet */
    CWallet* wallet{nullptr};
    /* the HD chain data model (external/internal chain counters) */
    CHDChain hdChain;
};

#endif //PIVX_SAPLINGSCRIPTPUBKEYMAN_H
