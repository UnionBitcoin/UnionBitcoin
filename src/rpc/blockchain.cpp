// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/blockchain.h>
#include <key.h>
#include <base58.h>
#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <checkpoints.h>
#include <coins.h>
#include <consensus/validation.h>
#include <validation.h>
#include <core_io.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <streams.h>
#include <sync.h>
#include <txdb.h>
#include <txmempool.h>
#include <util.h>
#include <utilstrencodings.h>
#include <hash.h>
#include <validationinterface.h>
#include <warnings.h>

#include <stdint.h>

#include <univalue.h>

#include <boost/thread/thread.hpp> // boost::thread::interrupt

#include <mutex>
#include <condition_variable>
#include <fstream>

#include <contract_storage/contract_storage.hpp>
#include <contract_engine/contract_helper.hpp>
#include <contract_engine/native_contract.hpp>
#include <fjson/crypto/base64.hpp>
#include <boost/scope_exit.hpp>
#include <boost/lexical_cast.hpp>

struct CUpdatedBlock
{
    uint256 hash;
    int height;
};
std::set<std::string> whitelist;
std::set<CScript> whitescriptlist;

static std::mutex cs_blockchange;
static std::condition_variable cond_blockchange;
static CUpdatedBlock latestblock;
extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);

double GetPowDifficulty(const CBlockIndex* blockindex)
{
    if (blockindex == nullptr)
    {
        if (chainActive.Tip() == nullptr)
            return 1.0;
        else
            blockindex = GetLastBlockIndex(chainActive.Tip(), false, Params().GetConsensus());
    }

    int nShift = (blockindex->nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

double GetPosDifficulty(const CBlockIndex* blockindex)
{
    if (blockindex == nullptr)
    {
        if (chainActive.Tip() == nullptr)
            return 1.0;
        else
        {
            blockindex = GetLastBlockIndex(chainActive.Tip(), true, Params().GetConsensus());
            if (blockindex == nullptr)
            	return 1.0;
        }
    }

    int nShift = (blockindex->nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}




double GetDifficulty(const CChain& chain, const CBlockIndex* blockindex)
{
    if (blockindex == nullptr)
    {
        if (chain.Tip() == nullptr)
            return 1.0;
        else
            blockindex = chain.Tip();
    }

    int nShift = (blockindex->nBits >> 24) & 0xff;
    double dDiff =
        (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

double GetDifficulty(const CBlockIndex* blockindex)
{
    return GetDifficulty(chainActive, blockindex);
}

UniValue blockheaderToJSON(const CBlockIndex* blockindex)
{
    AssertLockHeld(cs_main);
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", blockindex->nVersion));
    result.push_back(Pair("versionHex", strprintf("%08x", blockindex->nVersion)));
    result.push_back(Pair("merkleroot", blockindex->hashMerkleRoot.GetHex()));
    result.push_back(Pair("time", (int64_t)blockindex->nTime));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
    result.push_back(Pair("nonce", (uint64_t)blockindex->nNonce));
    result.push_back(Pair("bits", strprintf("%08x", blockindex->nBits)));
    result.push_back(Pair("difficulty", (blockindex->nVersion & MINING_TYPE_POS)?GetPosDifficulty(blockindex):GetPowDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    return result;
}

UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails)
{
    AssertLockHeld(cs_main);
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("strippedsize", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS)));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("weight", (int)::GetBlockWeight(block)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("versionHex", strprintf("%08x", block.nVersion)));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    UniValue txs(UniValue::VARR);
    for(const auto& tx : block.vtx)
    {
        if(txDetails)
        {
            UniValue objTx(UniValue::VOBJ);
            TxToUniv(*tx, uint256(), objTx, true, RPCSerializationFlags());
            txs.push_back(objTx);
        }
        else
            txs.push_back(tx->GetHash().GetHex());
    }
    result.push_back(Pair("tx", txs));
    result.push_back(Pair("time", block.GetBlockTime()));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("difficulty", (blockindex->nVersion & MINING_TYPE_POS)?GetPosDifficulty(blockindex): GetPowDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    return result;
}

UniValue getblockcount(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getblockcount\n"
            "\nReturns the number of blocks in the longest blockchain.\n"
            "\nResult:\n"
            "n    (numeric) The current block count\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockcount", "")
            + HelpExampleRpc("getblockcount", "")
        );

    LOCK(cs_main);
    return chainActive.Height();
}

UniValue getbestblockhash(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getbestblockhash\n"
            "\nReturns the hash of the best (tip) block in the longest blockchain.\n"
            "\nResult:\n"
            "\"hex\"      (string) the block hash hex encoded\n"
            "\nExamples:\n"
            + HelpExampleCli("getbestblockhash", "")
            + HelpExampleRpc("getbestblockhash", "")
        );

    LOCK(cs_main);
    return chainActive.Tip()->GetBlockHash().GetHex();
}

void RPCNotifyBlockChange(bool ibd, const CBlockIndex * pindex)
{
    if(pindex) {
        std::lock_guard<std::mutex> lock(cs_blockchange);
        latestblock.hash = pindex->GetBlockHash();
        latestblock.height = pindex->nHeight;
    }
    cond_blockchange.notify_all();
}

UniValue waitfornewblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "waitfornewblock (timeout)\n"
            "\nWaits for a specific new block and returns useful info about it.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. timeout (int, optional, default=0) Time in milliseconds to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitfornewblock", "1000")
            + HelpExampleRpc("waitfornewblock", "1000")
        );
    int timeout = 0;
    if (!request.params[0].isNull())
        timeout = request.params[0].get_int();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        block = latestblock;
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&block]{return latestblock.height != block.height || latestblock.hash != block.hash || !IsRPCRunning(); });
        else
            cond_blockchange.wait(lock, [&block]{return latestblock.height != block.height || latestblock.hash != block.hash || !IsRPCRunning(); });
        block = latestblock;
    }
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", block.hash.GetHex()));
    ret.push_back(Pair("height", block.height));
    return ret;
}

UniValue waitforblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "waitforblock <blockhash> (timeout)\n"
            "\nWaits for a specific new block and returns useful info about it.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. \"blockhash\" (required, string) Block hash to wait for.\n"
            "2. timeout       (int, optional, default=0) Time in milliseconds to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitforblock", "\"0000000000079f8ef3d2c688c244eb7a4570b24c9ed7b4a8c619eb02596f8862\", 1000")
            + HelpExampleRpc("waitforblock", "\"0000000000079f8ef3d2c688c244eb7a4570b24c9ed7b4a8c619eb02596f8862\", 1000")
        );
    int timeout = 0;

    uint256 hash = uint256S(request.params[0].get_str());

    if (!request.params[1].isNull())
        timeout = request.params[1].get_int();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&hash]{return latestblock.hash == hash || !IsRPCRunning();});
        else
            cond_blockchange.wait(lock, [&hash]{return latestblock.hash == hash || !IsRPCRunning(); });
        block = latestblock;
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", block.hash.GetHex()));
    ret.push_back(Pair("height", block.height));
    return ret;
}

UniValue waitforblockheight(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "waitforblockheight <height> (timeout)\n"
            "\nWaits for (at least) block height and returns the height and hash\n"
            "of the current tip.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. height  (required, int) Block height to wait for (int)\n"
            "2. timeout (int, optional, default=0) Time in milliseconds to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitforblockheight", "\"100\", 1000")
            + HelpExampleRpc("waitforblockheight", "\"100\", 1000")
        );
    int timeout = 0;

    int height = request.params[0].get_int();

    if (!request.params[1].isNull())
        timeout = request.params[1].get_int();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&height]{return latestblock.height >= height || !IsRPCRunning();});
        else
            cond_blockchange.wait(lock, [&height]{return latestblock.height >= height || !IsRPCRunning(); });
        block = latestblock;
    }
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", block.hash.GetHex()));
    ret.push_back(Pair("height", block.height));
    return ret;
}

UniValue syncwithvalidationinterfacequeue(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0) {
        throw std::runtime_error(
            "syncwithvalidationinterfacequeue\n"
            "\nWaits for the validation interface queue to catch up on everything that was there when we entered this function.\n"
            "\nExamples:\n"
            + HelpExampleCli("syncwithvalidationinterfacequeue","")
            + HelpExampleRpc("syncwithvalidationinterfacequeue","")
        );
    }
    SyncWithValidationInterfaceQueue();
    return NullUniValue;
}

UniValue getdifficulty(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getdifficulty\n"
            "\nReturns the last block difficulty as a multiple of the minimum difficulty.\n"
            "\nResult:\n"
            "n.nnn       (numeric) the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nExamples:\n"
            + HelpExampleCli("getdifficulty", "")
            + HelpExampleRpc("getdifficulty", "")
        );

    LOCK(cs_main);
    return GetDifficulty();
}


UniValue getpowdifficulty(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getpowdifficulty\n"
            "\nReturns the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nResult:\n"
            "n.nnn       (numeric) the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nExamples:\n"
            + HelpExampleCli("getpowdifficulty", "")
            + HelpExampleRpc("getpowdifficulty", "")
        );

    LOCK(cs_main);
    
    return GetPowDifficulty();
}

UniValue getposdifficulty(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getposdifficulty\n"
            "\nReturns the proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nResult:\n"
            "n.nnn       (numeric) the proof-of-stake difficulty as a multiple of the minimum difficulty.\n"
            "\nExamples:\n"
            + HelpExampleCli("getposdifficulty", "")
            + HelpExampleRpc("getposdifficulty", "")
        );

    LOCK(cs_main);
    return GetPosDifficulty();
}


std::string EntryDescriptionString()
{
    return "    \"size\" : n,             (numeric) virtual transaction size as defined in BIP 141. This is different from actual serialized size for witness transactions as witness data is discounted.\n"
           "    \"fee\" : n,              (numeric) transaction fee in " + CURRENCY_UNIT + "\n"
           "    \"modifiedfee\" : n,      (numeric) transaction fee with fee deltas used for mining priority\n"
           "    \"time\" : n,             (numeric) local time transaction entered pool in seconds since 1 Jan 1970 GMT\n"
           "    \"height\" : n,           (numeric) block height when transaction entered pool\n"
           "    \"descendantcount\" : n,  (numeric) number of in-mempool descendant transactions (including this one)\n"
           "    \"descendantsize\" : n,   (numeric) virtual transaction size of in-mempool descendants (including this one)\n"
           "    \"descendantfees\" : n,   (numeric) modified fees (see above) of in-mempool descendants (including this one)\n"
           "    \"ancestorcount\" : n,    (numeric) number of in-mempool ancestor transactions (including this one)\n"
           "    \"ancestorsize\" : n,     (numeric) virtual transaction size of in-mempool ancestors (including this one)\n"
           "    \"ancestorfees\" : n,     (numeric) modified fees (see above) of in-mempool ancestors (including this one)\n"
           "    \"wtxid\" : hash,         (string) hash of serialized transaction, including witness data\n"
           "    \"depends\" : [           (array) unconfirmed transactions used as inputs for this transaction\n"
           "        \"transactionid\",    (string) parent transaction id\n"
           "       ... ]\n";
}

void entryToJSON(UniValue &info, const CTxMemPoolEntry &e)
{
    AssertLockHeld(mempool.cs);

    info.push_back(Pair("size", (int)e.GetTxSize()));
    info.push_back(Pair("fee", ValueFromAmount(e.GetFee())));
    info.push_back(Pair("modifiedfee", ValueFromAmount(e.GetModifiedFee())));
    info.push_back(Pair("time", e.GetTime()));
    info.push_back(Pair("height", (int)e.GetHeight()));
    info.push_back(Pair("descendantcount", e.GetCountWithDescendants()));
    info.push_back(Pair("descendantsize", e.GetSizeWithDescendants()));
    info.push_back(Pair("descendantfees", e.GetModFeesWithDescendants()));
    info.push_back(Pair("ancestorcount", e.GetCountWithAncestors()));
    info.push_back(Pair("ancestorsize", e.GetSizeWithAncestors()));
    info.push_back(Pair("ancestorfees", e.GetModFeesWithAncestors()));
    info.push_back(Pair("wtxid", mempool.vTxHashes[e.vTxHashesIdx].first.ToString()));
    const CTransaction& tx = e.GetTx();
    std::set<std::string> setDepends;
    for (const CTxIn& txin : tx.vin)
    {
        if (mempool.exists(txin.prevout.hash))
            setDepends.insert(txin.prevout.hash.ToString());
    }

    UniValue depends(UniValue::VARR);
    for (const std::string& dep : setDepends)
    {
        depends.push_back(dep);
    }

    info.push_back(Pair("depends", depends));
}

UniValue mempoolToJSON(bool fVerbose)
{
    if (fVerbose)
    {
        LOCK(mempool.cs);
        UniValue o(UniValue::VOBJ);
        for (const CTxMemPoolEntry& e : mempool.mapTx)
        {
            const uint256& hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.push_back(Pair(hash.ToString(), info));
        }
        return o;
    }
    else
    {
        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);

        UniValue a(UniValue::VARR);
        for (const uint256& hash : vtxid)
            a.push_back(hash.ToString());

        return a;
    }
}

UniValue getrawmempool(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getrawmempool ( verbose )\n"
            "\nReturns all transaction ids in memory pool as a json array of string transaction ids.\n"
            "\nHint: use getmempoolentry to fetch a specific transaction from the mempool.\n"
            "\nArguments:\n"
            "1. verbose (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult: (for verbose = false):\n"
            "[                     (json array of string)\n"
            "  \"transactionid\"     (string) The transaction id\n"
            "  ,...\n"
            "]\n"
            "\nResult: (for verbose = true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getrawmempool", "true")
            + HelpExampleRpc("getrawmempool", "true")
        );

    bool fVerbose = false;
    if (!request.params[0].isNull())
        fVerbose = request.params[0].get_bool();

    return mempoolToJSON(fVerbose);
}

UniValue getmempoolancestors(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            "getmempoolancestors txid (verbose)\n"
            "\nIf txid is in the mempool, returns all in-mempool ancestors.\n"
            "\nArguments:\n"
            "1. \"txid\"                 (string, required) The transaction id (must be in mempool)\n"
            "2. verbose                  (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult (for verbose=false):\n"
            "[                       (json array of strings)\n"
            "  \"transactionid\"           (string) The transaction id of an in-mempool ancestor transaction\n"
            "  ,...\n"
            "]\n"
            "\nResult (for verbose=true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolancestors", "\"mytxid\"")
            + HelpExampleRpc("getmempoolancestors", "\"mytxid\"")
            );
    }

    bool fVerbose = false;
    if (!request.params[1].isNull())
        fVerbose = request.params[1].get_bool();

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    CTxMemPool::setEntries setAncestors;
    uint64_t noLimit = std::numeric_limits<uint64_t>::max();
    std::string dummy;
    mempool.CalculateMemPoolAncestors(*it, setAncestors, noLimit, noLimit, noLimit, noLimit, dummy, false);

    if (!fVerbose) {
        UniValue o(UniValue::VARR);
        for (CTxMemPool::txiter ancestorIt : setAncestors) {
            o.push_back(ancestorIt->GetTx().GetHash().ToString());
        }

        return o;
    } else {
        UniValue o(UniValue::VOBJ);
        for (CTxMemPool::txiter ancestorIt : setAncestors) {
            const CTxMemPoolEntry &e = *ancestorIt;
            const uint256& _hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.push_back(Pair(_hash.ToString(), info));
        }
        return o;
    }
}

UniValue getmempooldescendants(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            "getmempooldescendants txid (verbose)\n"
            "\nIf txid is in the mempool, returns all in-mempool descendants.\n"
            "\nArguments:\n"
            "1. \"txid\"                 (string, required) The transaction id (must be in mempool)\n"
            "2. verbose                  (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult (for verbose=false):\n"
            "[                       (json array of strings)\n"
            "  \"transactionid\"           (string) The transaction id of an in-mempool descendant transaction\n"
            "  ,...\n"
            "]\n"
            "\nResult (for verbose=true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempooldescendants", "\"mytxid\"")
            + HelpExampleRpc("getmempooldescendants", "\"mytxid\"")
            );
    }

    bool fVerbose = false;
    if (!request.params[1].isNull())
        fVerbose = request.params[1].get_bool();

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    CTxMemPool::setEntries setDescendants;
    mempool.CalculateDescendants(it, setDescendants);
    // CTxMemPool::CalculateDescendants will include the given tx
    setDescendants.erase(it);

    if (!fVerbose) {
        UniValue o(UniValue::VARR);
        for (CTxMemPool::txiter descendantIt : setDescendants) {
            o.push_back(descendantIt->GetTx().GetHash().ToString());
        }

        return o;
    } else {
        UniValue o(UniValue::VOBJ);
        for (CTxMemPool::txiter descendantIt : setDescendants) {
            const CTxMemPoolEntry &e = *descendantIt;
            const uint256& _hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.push_back(Pair(_hash.ToString(), info));
        }
        return o;
    }
}

UniValue getmempoolentry(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "getmempoolentry txid\n"
            "\nReturns mempool data for given transaction\n"
            "\nArguments:\n"
            "1. \"txid\"                   (string, required) The transaction id (must be in mempool)\n"
            "\nResult:\n"
            "{                           (json object)\n"
            + EntryDescriptionString()
            + "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolentry", "\"mytxid\"")
            + HelpExampleRpc("getmempoolentry", "\"mytxid\"")
        );
    }

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    const CTxMemPoolEntry &e = *it;
    UniValue info(UniValue::VOBJ);
    entryToJSON(info, e);
    return info;
}

UniValue getblockhash(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getblockhash height\n"
            "\nReturns hash of block in best-block-chain at height provided.\n"
            "\nArguments:\n"
            "1. height         (numeric, required) The height index\n"
            "\nResult:\n"
            "\"hash\"         (string) The block hash\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockhash", "1000")
            + HelpExampleRpc("getblockhash", "1000")
        );

    LOCK(cs_main);

    int nHeight = request.params[0].get_int();
    if (nHeight < 0 || nHeight > chainActive.Height())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

    CBlockIndex* pblockindex = chainActive[nHeight];
    return pblockindex->GetBlockHash().GetHex();
}

UniValue getblockheader(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getblockheader \"hash\" ( verbose )\n"
            "\nIf verbose is false, returns a string that is serialized, hex-encoded data for blockheader 'hash'.\n"
            "If verbose is true, returns an Object with information about blockheader <hash>.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. verbose           (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"0000...1f3\"     (string) Expected number of hashes required to produce the current chain (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\",      (string) The hash of the next block\n"
            "}\n"
            "\nResult (for verbose=false):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + HelpExampleRpc("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
        );

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (!request.params[1].isNull())
        fVerbose = request.params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (!fVerbose)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << pblockindex->GetBlockHeader();
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockheaderToJSON(pblockindex);
}

UniValue getblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getblock \"blockhash\" ( verbosity ) \n"
            "\nIf verbosity is 0, returns a string that is serialized, hex-encoded data for block 'hash'.\n"
            "If verbosity is 1, returns an Object with information about block <hash>.\n"
            "If verbosity is 2, returns an Object with information about block <hash> and information about each transaction. \n"
            "\nArguments:\n"
            "1. \"blockhash\"          (string, required) The block hash\n"
            "2. verbosity              (numeric, optional, default=1) 0 for hex encoded data, 1 for a json object, and 2 for json object with transaction data\n"
            "\nResult (for verbosity = 0):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nResult (for verbosity = 1):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"strippedsize\" : n,    (numeric) The block size excluding witness data\n"
            "  \"weight\" : n           (numeric) The block weight as defined in BIP 141\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"tx\" : [               (array of string) The transaction ids\n"
            "     \"transactionid\"     (string) The transaction id\n"
            "     ,...\n"
            "  ],\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"xxxx\",  (string) Expected number of hashes required to produce the chain up to this block (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the next block\n"
            "}\n"
            "\nResult (for verbosity = 2):\n"
            "{\n"
            "  ...,                     Same output as verbosity = 1.\n"
            "  \"tx\" : [               (array of Objects) The transactions in the format of the getrawtransaction RPC. Different from verbosity = 1 \"tx\" result.\n"
            "         ,...\n"
            "  ],\n"
            "  ,...                     Same output as verbosity = 1.\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getblock", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + HelpExampleRpc("getblock", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
        );

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    int verbosity = 1;
    if (!request.params[1].isNull()) {
        if(request.params[1].isNum())
            verbosity = request.params[1].get_int();
        else
            verbosity = request.params[1].get_bool() ? 1 : 0;
    }

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_MISC_ERROR, "Block not available (pruned data)");

    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        // Block not found on disk. This could be because we have the block
        // header in our index but don't have the block (for example if a
        // non-whitelisted node sends us an unrequested long chain of valid
        // blocks, we add the headers to our index, but don't accept the
        // block).
        throw JSONRPCError(RPC_MISC_ERROR, "Block not found on disk");

    if (verbosity <= 0)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION | RPCSerializationFlags());
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockToJSON(block, pblockindex, verbosity >= 2);
}

struct CCoinsStats
{
    int nHeight;
    uint256 hashBlock;
    uint64_t nTransactions;
    uint64_t nTransactionOutputs;
    uint64_t nBogoSize;
    uint256 hashSerialized;
    uint64_t nDiskSize;
    CAmount nTotalAmount;

    CCoinsStats() : nHeight(0), nTransactions(0), nTransactionOutputs(0), nBogoSize(0), nDiskSize(0), nTotalAmount(0) {}
};

static void ApplyStats(CCoinsStats &stats, CHashWriter& ss, const uint256& hash, const std::map<uint32_t, Coin>& outputs)
{
    assert(!outputs.empty());
    ss << hash;
    ss << VARINT(outputs.begin()->second.nHeight * 2 + outputs.begin()->second.fCoinBase);
    stats.nTransactions++;
    for (const auto output : outputs) {
        ss << VARINT(output.first + 1);
        ss << output.second.out.scriptPubKey;
        ss << VARINT(output.second.out.nValue);
        stats.nTransactionOutputs++;
        stats.nTotalAmount += output.second.out.nValue;
        stats.nBogoSize += 32 /* txid */ + 4 /* vout index */ + 4 /* height + coinbase */ + 8 /* amount */ +
                           2 /* scriptPubKey len */ + output.second.out.scriptPubKey.size() /* scriptPubKey */;
    }
    ss << VARINT(0);
}

CTransactionRef get_trx(const uint256& hash)
{
    CTransactionRef tx;
    uint256 hashBlock;
    if (!GetTransaction(hash, tx, Params().GetConsensus(), hashBlock, true))
        return CTransactionRef();
    return tx;
}

void get_destinations_from_vin(std::set<CTxDestination>& result, const std::vector<CTxIn>& vins)
{
    for (const auto vin : vins)
    {
        COutPoint prevout = vin.prevout;
        CTransactionRef tx = get_trx(prevout.hash);
        if (!tx)
            return;
        std::vector<CTxOut> vout = tx->vout;
        txnouttype type;
        std::vector<CTxDestination> addresses;
        int nRequired;
        ExtractDestinations(vout[prevout.n].scriptPubKey, type, addresses, nRequired);
        for (auto addr: addresses)
        {
            result.insert(addr);
        }
    }
}

static void get_whitelist_impl(const std::vector<CTxIn>& vin, const std::vector<CTxOut>& outputs, std::set<CTxDestination>& result)
{
    std::set<CTxDestination> addrs;
    get_destinations_from_vin(addrs, vin);
    for (const auto output : outputs) {
        txnouttype type;
        std::vector<CTxDestination> addresses;
        int nRequired;
        try
        {
            ExtractDestinations(output.scriptPubKey, type, addresses, nRequired);
            for (auto addr: addresses)
            {
                if (addrs.end() != addrs.find(addr))
                {
                    auto str = EncodeDestination(addr);
                    printf("%s\n",str.c_str());
                }
            }
        }
        catch(...){
        }
		
    }	
}

static void get_whitelist(std::set<CTxDestination> &result, int64_t last=0)
{
    int64_t nHeight = last+144;//chainActive.Height();
   
    if (nHeight > chainActive.Height())
        nHeight = chainActive.Height();
    if (nHeight > Params().GetConsensus().UBCHeight - 1)
        nHeight = Params().GetConsensus().UBCHeight - 1;
    while(nHeight >= last)
    {
        CBlockIndex* pblockindex_t = chainActive[nHeight];
        CBlock block;
        CBlockIndex* pblockindex = mapBlockIndex[pblockindex_t->GetBlockHash()];
        ReadBlockFromDisk(block, pblockindex, Params().GetConsensus());
        std::vector<CTransactionRef> vtx = block.vtx;
        for (auto tx : vtx)
        {
            if(tx->IsCoinBase())
                continue;
            get_whitelist_impl(tx->vin, tx->vout, result);
        }
        nHeight--;
    }
}

UniValue getwhitelist(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
                "getwhitelist blocknum\n"
                "\nReturns hash of block in best-block-chain at height provided.\n"
                "\nArguments:\n"
                "1. blocknum		   (numeric, required) The blocknum index\n"
                "\nResult:\n"
                "\"address list\"		  (vector<string>) The address list\n"
                "\nExamples:\n"
                + HelpExampleCli("getwhitelist", "1000")
                + HelpExampleRpc("getwhitelist", "1000")
       );

    LOCK(cs_main);
	int last = request.params[0].get_int();
    UniValue ret(UniValue::VARR);
    FlushStateToDisk();
    std::set<CTxDestination> result;
    get_whitelist(result, last);
	fflush(stdout);
    return ret;
}


UniValue readwhitelist(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
                "readwhitelist filename\n"
                "\nReturns hash of block in best-block-chain at height provided.\n"
                "\nArguments:\n"
                "1. filename		   (string, required) The blocknum index\n"
                "\nResult:\n"
                "\"\"		  (void) The address list\n"
                "\nExamples:\n"
                + HelpExampleCli("readwhitelist", "a.txt")
                + HelpExampleRpc("readwhitelist", "a.txt")
       );

    LOCK(cs_main);
	std::string file = request.params[0].get_str();
    UniValue ret(UniValue::VNULL);
	
	char buffer[1024];
	std::ifstream in(file);
	if (!in.is_open())
	{
	    throw JSONRPCError(RPC_MISC_ERROR, "file doesnt exist.");
	}
	
	while (!in.eof())
	{
		in.getline(buffer, 1024);
		whitelist.insert(buffer);
	}
	in.close();
    return ret;
}




//! Calculate statistics about the unspent transaction output set
static bool GetUTXOStats(CCoinsView *view, CCoinsStats &stats)
{
    std::unique_ptr<CCoinsViewCursor> pcursor(view->Cursor());
    assert(pcursor);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = pcursor->GetBestBlock();
    {
        LOCK(cs_main);
        stats.nHeight = mapBlockIndex.find(stats.hashBlock)->second->nHeight;
    }
    ss << stats.hashBlock;
    uint256 prevkey;
    std::map<uint32_t, Coin> outputs;
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        COutPoint key;
        Coin coin;
        if (pcursor->GetKey(key) && pcursor->GetValue(coin)) {
            if (!outputs.empty() && key.hash != prevkey) {
                ApplyStats(stats, ss, prevkey, outputs);
                outputs.clear();
            }
            prevkey = key.hash;
            outputs[key.n] = std::move(coin);
        } else {
            return error("%s: unable to read value", __func__);
        }
        pcursor->Next();
    }
    if (!outputs.empty()) {
        ApplyStats(stats, ss, prevkey, outputs);
    }
    stats.hashSerialized = ss.GetHash();
    stats.nDiskSize = view->EstimateSize();
    return true;
}

int GetHolyUTXO(int count, std::vector<std::pair<COutPoint, CTxOut>>& outputs)
{
	FlushStateToDisk();
    std::unique_ptr<CCoinsViewCursor> pcursor(pcoinsdbview->Cursor());
	int index = 0;

    outputs.clear();

	int chainHeight = chainActive.Height();
	if ((chainHeight < Params().GetConsensus().UBCHeight - 1) 
		|| (chainHeight >= (Params().GetConsensus().UBCHeight + Params().GetConsensus().UBCInitBlockCount - 1)))
		return 0;

	// generator's address
	std::vector<unsigned char> data;
	data = ParseHex(Params().GetConsensus().UBCForkGeneratorPubkey);
	CPubKey PubKey(data);
	CKeyID KeyID = PubKey.GetID();
	int nSpendHeight = chainActive.Height() + 1;
	
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        COutPoint key;
        Coin coin;
        if (pcursor->GetKey(key) && pcursor->GetValue(coin)) {
			if (coin.nHeight >= Params().GetConsensus().UBCHeight) {
				pcursor->Next();
				continue;
			}
			// ignore amount less than 0.01
			if (coin.out.nValue <= 1000000) {
				pcursor->Next();
				continue;
			}
			// ignore not mature coin
			if (coin.IsCoinBase() && nSpendHeight - coin.nHeight < getCoinBaseMaturity(coin.nHeight)) {
				pcursor->Next();
				continue;
			}	
			txnouttype typeRet;
			std::vector<CTxDestination> addressRet;
			int nRequiredRet;
			bool ret = ExtractDestinations(coin.out.scriptPubKey, typeRet, addressRet, nRequiredRet);
			if (ret) {				
				if (addressRet.size() == 1){
					std::string addrStr = EncodeDestination(addressRet[0]);
					// judge if the lock script is belong to block UBCForkGenerator
					std::string KeyIDStr = EncodeDestination(KeyID);
					if (KeyIDStr == addrStr) {
						pcursor->Next();
						continue;
					}
					// judge if the lock script owner is in the whitelist
					if (whitelist.find(addrStr) == whitelist.end()) {
						outputs.emplace_back(std::make_pair(key, coin.out));
						++index;
						if (index >= count) break;
					}
				}
				else if (addressRet.size() > 1) {
					// judge if the lock MS script is in the whitescriptlist
					if (whitescriptlist.find(coin.out.scriptPubKey) == whitescriptlist.end()) {
						outputs.emplace_back(std::make_pair(key, coin.out));
						++index;
						if (index >= count) break;					
					}
				}
				else {
					;
				}
			}
        }
        pcursor->Next();
    }

    return outputs.size();

}


UniValue pruneblockchain(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "pruneblockchain\n"
            "\nArguments:\n"
            "1. \"height\"       (numeric, required) The block height to prune up to. May be set to a discrete height, or a unix timestamp\n"
            "                  to prune blocks whose block time is at least 2 hours older than the provided timestamp.\n"
            "\nResult:\n"
            "n    (numeric) Height of the last block pruned.\n"
            "\nExamples:\n"
            + HelpExampleCli("pruneblockchain", "1000")
            + HelpExampleRpc("pruneblockchain", "1000"));

    if (!fPruneMode)
        throw JSONRPCError(RPC_MISC_ERROR, "Cannot prune blocks because node is not in prune mode.");

    LOCK(cs_main);

    int heightParam = request.params[0].get_int();
    if (heightParam < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative block height.");

    // Height value more than a billion is too high to be a block height, and
    // too low to be a block time (corresponds to timestamp from Sep 2001).
    if (heightParam > 1000000000) {
        // Add a 2 hour buffer to include blocks which might have had old timestamps
        CBlockIndex* pindex = chainActive.FindEarliestAtLeast(heightParam - TIMESTAMP_WINDOW);
        if (!pindex) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Could not find block with at least the specified timestamp.");
        }
        heightParam = pindex->nHeight;
    }

    unsigned int height = (unsigned int) heightParam;
    unsigned int chainHeight = (unsigned int) chainActive.Height();
    if (chainHeight < Params().PruneAfterHeight())
        throw JSONRPCError(RPC_MISC_ERROR, "Blockchain is too short for pruning.");
    else if (height > chainHeight)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Blockchain is shorter than the attempted prune height.");
    else if (height > chainHeight - MIN_BLOCKS_TO_KEEP) {
        LogPrint(BCLog::RPC, "Attempt to prune blocks close to the tip.  Retaining the minimum number of blocks.");
        height = chainHeight - MIN_BLOCKS_TO_KEEP;
    }

    PruneBlockFilesManual(height);
    return uint64_t(height);
}

UniValue gettxoutsetinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "gettxoutsetinfo\n"
            "\nReturns statistics about the unspent transaction output set.\n"
            "Note this call may take some time.\n"
            "\nResult:\n"
            "{\n"
            "  \"height\":n,     (numeric) The current block height (index)\n"
            "  \"bestblock\": \"hex\",   (string) The hash of the block at the tip of the chain\n"
            "  \"transactions\": n,      (numeric) The number of transactions with unspent outputs\n"
            "  \"txouts\": n,            (numeric) The number of unspent transaction outputs\n"
            "  \"bogosize\": n,          (numeric) A meaningless metric for UTXO set size\n"
            "  \"hash_serialized_2\": \"hash\", (string) The serialized hash\n"
            "  \"disk_size\": n,         (numeric) The estimated size of the chainstate on disk\n"
            "  \"total_amount\": x.xxx          (numeric) The total amount\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("gettxoutsetinfo", "")
            + HelpExampleRpc("gettxoutsetinfo", "")
        );

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    FlushStateToDisk();
    if (GetUTXOStats(pcoinsdbview.get(), stats)) {
        ret.push_back(Pair("height", (int64_t)stats.nHeight));
        ret.push_back(Pair("bestblock", stats.hashBlock.GetHex()));
        ret.push_back(Pair("transactions", (int64_t)stats.nTransactions));
        ret.push_back(Pair("txouts", (int64_t)stats.nTransactionOutputs));
        ret.push_back(Pair("bogosize", (int64_t)stats.nBogoSize));
        ret.push_back(Pair("hash_serialized_2", stats.hashSerialized.GetHex()));
        ret.push_back(Pair("disk_size", stats.nDiskSize));
        ret.push_back(Pair("total_amount", ValueFromAmount(stats.nTotalAmount)));
    } else {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to read UTXO set");
    }
    return ret;
}

UniValue getbalancetopn(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getbalancetopn\n"
            "\nReturns statistics about the top N address of balance .\n"
            "Note this call may take some time.\n"
            "\nArguments:\n"
            "1. \"topn\"             (numeric, optional) top address count\n"
            "\nResult:\n"
			"[\n"
			"  {\n"
			"	 \"address\": xxxx,		   (string) address\n"
			"	 \"amount\": \"xxxx\",		 (numeric) amount of balance\n"
			"  },\n"
			"  {\n"
			"	 \"address\": xxxx,\n"
			"	 \"amount\": \"xxxx\",\n"
			"  }\n"
			"]\n"

            "\nExamples:\n"
            + HelpExampleCli("getbalancetopn", "")
            + HelpExampleRpc("getbalancetopn", "")
        );

	unsigned int topn = 100;
	if (!request.params[0].isNull())
    	topn = request.params[0].get_int();
	std::map<std::string, double> addressBalanceMap;
	std::map<double, std::string, std::greater<double>> balanceAddressMap;

	FlushStateToDisk();
    std::unique_ptr<CCoinsViewCursor> pcursor(pcoinsdbview->Cursor());
	
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        COutPoint key;
        Coin coin;
        if (pcursor->GetKey(key) && pcursor->GetValue(coin)) {
			txnouttype typeRet;
			std::vector<CTxDestination> addressRet;
			int nRequiredRet;
			bool ret = ExtractDestinations(coin.out.scriptPubKey, typeRet, addressRet, nRequiredRet);
			if (ret) {				
				if (addressRet.size() == 1){
					std::string addrStr = EncodeDestination(addressRet[0]);

					// judge if the lock script owner is in the whitelist
					if (addressBalanceMap.find(addrStr) == addressBalanceMap.end())
						addressBalanceMap.insert(std::make_pair(addrStr, coin.out.nValue / 100000000.0));
					else
						addressBalanceMap[addrStr] = addressBalanceMap[addrStr] + (coin.out.nValue / 100000000.0);

				}
				else {
					;
				}
			}
        }
        pcursor->Next();
    }

	for(const auto& addrBalance: addressBalanceMap)
		balanceAddressMap.insert(std::make_pair(addrBalance.second, addrBalance.first));

	if (topn > balanceAddressMap.size())
		topn = balanceAddressMap.size();

	UniValue ret(UniValue::VARR);
	auto iter = balanceAddressMap.begin();
	while (topn > 0) {
		char a[64];
		char*p = a;
		snprintf(p, 64, "%.08f", iter->first);
		UniValue obj(UniValue::VOBJ);
		obj.push_back(Pair("address", (std::string)iter->second));
		obj.push_back(Pair("amount", std::string(a)));
		ret.push_back(obj);
		++iter;
		--topn;
	}

	return ret;
	
}


UniValue gettxout(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "gettxout \"txid\" n ( include_mempool )\n"
            "\nReturns details about an unspent transaction output.\n"
            "\nArguments:\n"
            "1. \"txid\"             (string, required) The transaction id\n"
            "2. \"n\"                (numeric, required) vout number\n"
            "3. \"include_mempool\"  (boolean, optional) Whether to include the mempool. Default: true."
            "     Note that an unspent output that is spent in the mempool won't appear.\n"
            "\nResult:\n"
            "{\n"
            "  \"bestblock\":  \"hash\",    (string) The hash of the block at the tip of the chain\n"
            "  \"confirmations\" : n,       (numeric) The number of confirmations\n"
            "  \"value\" : x.xxx,           (numeric) The transaction value in " + CURRENCY_UNIT + "\n"
            "  \"scriptPubKey\" : {         (json object)\n"
            "     \"asm\" : \"code\",       (string) \n"
            "     \"hex\" : \"hex\",        (string) \n"
            "     \"reqSigs\" : n,          (numeric) Number of required signatures\n"
            "     \"type\" : \"pubkeyhash\", (string) The type, eg pubkeyhash\n"
            "     \"addresses\" : [          (array of string) array of bitcoin addresses\n"
            "        \"address\"     (string) bitcoin address\n"
            "        ,...\n"
            "     ]\n"
            "  },\n"
            "  \"coinbase\" : true|false   (boolean) Coinbase or not\n"
            "}\n"

            "\nExamples:\n"
            "\nGet unspent transactions\n"
            + HelpExampleCli("listunspent", "") +
            "\nView the details\n"
            + HelpExampleCli("gettxout", "\"txid\" 1") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("gettxout", "\"txid\", 1")
        );

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    int n = request.params[1].get_int();
    COutPoint out(hash, n);
    bool fMempool = true;
    if (!request.params[2].isNull())
        fMempool = request.params[2].get_bool();

    Coin coin;
    if (fMempool) {
        LOCK(mempool.cs);
        CCoinsViewMemPool view(pcoinsTip.get(), mempool);
        if (!view.GetCoin(out, coin) || mempool.isSpent(out)) {
            return NullUniValue;
        }
    } else {
        if (!pcoinsTip->GetCoin(out, coin)) {
            return NullUniValue;
        }
    }

    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    CBlockIndex *pindex = it->second;
    ret.push_back(Pair("bestblock", pindex->GetBlockHash().GetHex()));
    if (coin.nHeight == MEMPOOL_HEIGHT) {
        ret.push_back(Pair("confirmations", 0));
    } else {
        ret.push_back(Pair("confirmations", (int64_t)(pindex->nHeight - coin.nHeight + 1)));
    }
    ret.push_back(Pair("value", ValueFromAmount(coin.out.nValue)));
    UniValue o(UniValue::VOBJ);
    ScriptPubKeyToUniv(coin.out.scriptPubKey, o, true);
    ret.push_back(Pair("scriptPubKey", o));
    ret.push_back(Pair("coinbase", (bool)coin.fCoinBase));

    return ret;
}

UniValue verifychain(const JSONRPCRequest& request)
{
    int nCheckLevel = gArgs.GetArg("-checklevel", DEFAULT_CHECKLEVEL);
    int nCheckDepth = gArgs.GetArg("-checkblocks", DEFAULT_CHECKBLOCKS);
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "verifychain ( checklevel nblocks )\n"
            "\nVerifies blockchain database.\n"
            "\nArguments:\n"
            "1. checklevel   (numeric, optional, 0-4, default=" + strprintf("%d", nCheckLevel) + ") How thorough the block verification is.\n"
            "2. nblocks      (numeric, optional, default=" + strprintf("%d", nCheckDepth) + ", 0=all) The number of blocks to check.\n"
            "\nResult:\n"
            "true|false       (boolean) Verified or not\n"
            "\nExamples:\n"
            + HelpExampleCli("verifychain", "")
            + HelpExampleRpc("verifychain", "")
        );

    LOCK(cs_main);

    if (!request.params[0].isNull())
        nCheckLevel = request.params[0].get_int();
    if (!request.params[1].isNull())
        nCheckDepth = request.params[1].get_int();

    return CVerifyDB().VerifyDB(Params(), pcoinsTip.get(), nCheckLevel, nCheckDepth);
}

/** Implementation of IsSuperMajority with better feedback */
static UniValue SoftForkMajorityDesc(int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    bool activated = false;
    switch(version)
    {
        case 2:
            activated = pindex->nHeight >= consensusParams.BIP34Height;
            break;
        case 3:
            activated = pindex->nHeight >= consensusParams.BIP66Height;
            break;
        case 4:
            activated = pindex->nHeight >= consensusParams.BIP65Height;
            break;
    }
    rv.push_back(Pair("status", activated));
    return rv;
}

static UniValue SoftForkDesc(const std::string &name, int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("id", name));
    rv.push_back(Pair("version", version));
    rv.push_back(Pair("reject", SoftForkMajorityDesc(version, pindex, consensusParams)));
    return rv;
}

static UniValue BIP9SoftForkDesc(const Consensus::Params& consensusParams, Consensus::DeploymentPos id)
{
    UniValue rv(UniValue::VOBJ);
    const ThresholdState thresholdState = VersionBitsTipState(consensusParams, id);
    switch (thresholdState) {
    case THRESHOLD_DEFINED: rv.push_back(Pair("status", "defined")); break;
    case THRESHOLD_STARTED: rv.push_back(Pair("status", "started")); break;
    case THRESHOLD_LOCKED_IN: rv.push_back(Pair("status", "locked_in")); break;
    case THRESHOLD_ACTIVE: rv.push_back(Pair("status", "active")); break;
    case THRESHOLD_FAILED: rv.push_back(Pair("status", "failed")); break;
    }
    if (THRESHOLD_STARTED == thresholdState)
    {
        rv.push_back(Pair("bit", consensusParams.vDeployments[id].bit));
    }
    rv.push_back(Pair("startTime", consensusParams.vDeployments[id].nStartTime));
    rv.push_back(Pair("timeout", consensusParams.vDeployments[id].nTimeout));
    rv.push_back(Pair("since", VersionBitsTipStateSinceHeight(consensusParams, id)));
    if (THRESHOLD_STARTED == thresholdState)
    {
        UniValue statsUV(UniValue::VOBJ);
        BIP9Stats statsStruct = VersionBitsTipStatistics(consensusParams, id);
        statsUV.push_back(Pair("period", statsStruct.period));
        statsUV.push_back(Pair("threshold", statsStruct.threshold));
        statsUV.push_back(Pair("elapsed", statsStruct.elapsed));
        statsUV.push_back(Pair("count", statsStruct.count));
        statsUV.push_back(Pair("possible", statsStruct.possible));
        rv.push_back(Pair("statistics", statsUV));
    }
    return rv;
}

void BIP9SoftForkDescPushBack(UniValue& bip9_softforks, const Consensus::Params& consensusParams, Consensus::DeploymentPos id)
{
    // Deployments with timeout value of 0 are hidden.
    // A timeout value of 0 guarantees a softfork will never be activated.
    // This is used when softfork codes are merged without specifying the deployment schedule.
    if (consensusParams.vDeployments[id].nTimeout > 0)
        bip9_softforks.push_back(Pair(VersionBitsDeploymentInfo[id].name, BIP9SoftForkDesc(consensusParams, id)));
}

UniValue getblockchaininfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getblockchaininfo\n"
            "Returns an object containing various state info regarding blockchain processing.\n"
            "\nResult:\n"
            "{\n"
            "  \"chain\": \"xxxx\",              (string) current network name as defined in BIP70 (main, test, regtest)\n"
            "  \"blocks\": xxxxxx,             (numeric) the current number of blocks processed in the server\n"
            "  \"headers\": xxxxxx,            (numeric) the current number of headers we have validated\n"
            "  \"bestblockhash\": \"...\",       (string) the hash of the currently best block\n"
            "  \"difficulty\": xxxxxx,         (numeric) the current difficulty\n"
            "  \"mediantime\": xxxxxx,         (numeric) median time for the current best block\n"
            "  \"verificationprogress\": xxxx, (numeric) estimate of verification progress [0..1]\n"
            "  \"initialblockdownload\": xxxx, (bool) (debug information) estimate of whether this node is in Initial Block Download mode.\n"
            "  \"chainwork\": \"xxxx\"           (string) total amount of work in active chain, in hexadecimal\n"
            "  \"size_on_disk\": xxxxxx,       (numeric) the estimated size of the block and undo files on disk\n"
            "  \"pruned\": xx,                 (boolean) if the blocks are subject to pruning\n"
            "  \"pruneheight\": xxxxxx,        (numeric) lowest-height complete block stored (only present if pruning is enabled)\n"
            "  \"automatic_pruning\": xx,      (boolean) whether automatic pruning is enabled (only present if pruning is enabled)\n"
            "  \"prune_target_size\": xxxxxx,  (numeric) the target size used by pruning (only present if automatic pruning is enabled)\n"
            "  \"softforks\": [                (array) status of softforks in progress\n"
            "     {\n"
            "        \"id\": \"xxxx\",           (string) name of softfork\n"
            "        \"version\": xx,          (numeric) block version\n"
            "        \"reject\": {             (object) progress toward rejecting pre-softfork blocks\n"
            "           \"status\": xx,        (boolean) true if threshold reached\n"
            "        },\n"
            "     }, ...\n"
            "  ],\n"
            "  \"bip9_softforks\": {           (object) status of BIP9 softforks in progress\n"
            "     \"xxxx\" : {                 (string) name of the softfork\n"
            "        \"status\": \"xxxx\",       (string) one of \"defined\", \"started\", \"locked_in\", \"active\", \"failed\"\n"
            "        \"bit\": xx,              (numeric) the bit (0-28) in the block version field used to signal this softfork (only for \"started\" status)\n"
            "        \"startTime\": xx,        (numeric) the minimum median time past of a block at which the bit gains its meaning\n"
            "        \"timeout\": xx,          (numeric) the median time past of a block at which the deployment is considered failed if not yet locked in\n"
            "        \"since\": xx,            (numeric) height of the first block to which the status applies\n"
            "        \"statistics\": {         (object) numeric statistics about BIP9 signalling for a softfork (only for \"started\" status)\n"
            "           \"period\": xx,        (numeric) the length in blocks of the BIP9 signalling period \n"
            "           \"threshold\": xx,     (numeric) the number of blocks with the version bit set required to activate the feature \n"
            "           \"elapsed\": xx,       (numeric) the number of blocks elapsed since the beginning of the current period \n"
            "           \"count\": xx,         (numeric) the number of blocks with the version bit set in the current period \n"
            "           \"possible\": xx       (boolean) returns false if there are not enough blocks left in this period to pass activation threshold \n"
            "        }\n"
            "     }\n"
            "  }\n"
            "  \"warnings\" : \"...\",           (string) any network and blockchain warnings.\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockchaininfo", "")
            + HelpExampleRpc("getblockchaininfo", "")
        );

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("chain",                 Params().NetworkIDString()));
    obj.push_back(Pair("blocks",                (int)chainActive.Height()));
    obj.push_back(Pair("headers",               pindexBestHeader ? pindexBestHeader->nHeight : -1));
    obj.push_back(Pair("bestblockhash",         chainActive.Tip()->GetBlockHash().GetHex()));
    obj.push_back(Pair("difficulty",            (double)GetDifficulty()));
    obj.push_back(Pair("mediantime",            (int64_t)chainActive.Tip()->GetMedianTimePast()));
    obj.push_back(Pair("verificationprogress",  GuessVerificationProgress(Params().TxData(), chainActive.Tip())));
    obj.push_back(Pair("initialblockdownload",  IsInitialBlockDownload()));
    obj.push_back(Pair("chainwork",             chainActive.Tip()->nChainWork.GetHex()));
    obj.push_back(Pair("size_on_disk",          CalculateCurrentUsage()));
    obj.push_back(Pair("pruned",                fPruneMode));
    if (fPruneMode) {
        CBlockIndex* block = chainActive.Tip();
        assert(block);
        while (block->pprev && (block->pprev->nStatus & BLOCK_HAVE_DATA)) {
            block = block->pprev;
        }

        obj.push_back(Pair("pruneheight",        block->nHeight));

        // if 0, execution bypasses the whole if block.
        bool automatic_pruning = (gArgs.GetArg("-prune", 0) != 1);
        obj.push_back(Pair("automatic_pruning",  automatic_pruning));
        if (automatic_pruning) {
            obj.push_back(Pair("prune_target_size",  nPruneTarget));
        }
    }

    const Consensus::Params& consensusParams = Params().GetConsensus();
    CBlockIndex* tip = chainActive.Tip();
    UniValue softforks(UniValue::VARR);
    UniValue bip9_softforks(UniValue::VOBJ);
    softforks.push_back(SoftForkDesc("bip34", 2, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip66", 3, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip65", 4, tip, consensusParams));
    for (int pos = Consensus::DEPLOYMENT_CSV; pos != Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++pos) {
        BIP9SoftForkDescPushBack(bip9_softforks, consensusParams, static_cast<Consensus::DeploymentPos>(pos));
    }
    obj.push_back(Pair("softforks",             softforks));
    obj.push_back(Pair("bip9_softforks", bip9_softforks));

    obj.push_back(Pair("warnings", GetWarnings("statusbar")));
    return obj;
}

/** Comparison function for sorting the getchaintips heads.  */
struct CompareBlocksByHeight
{
    bool operator()(const CBlockIndex* a, const CBlockIndex* b) const
    {
        /* Make sure that unequal blocks with the same height do not compare
           equal. Use the pointers themselves to make a distinction. */

        if (a->nHeight != b->nHeight)
          return (a->nHeight > b->nHeight);

        return a < b;
    }
};

UniValue getchaintips(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getchaintips\n"
            "Return information about all known tips in the block tree,"
            " including the main chain as well as orphaned branches.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"height\": xxxx,         (numeric) height of the chain tip\n"
            "    \"hash\": \"xxxx\",         (string) block hash of the tip\n"
            "    \"branchlen\": 0          (numeric) zero for main chain\n"
            "    \"status\": \"active\"      (string) \"active\" for the main chain\n"
            "  },\n"
            "  {\n"
            "    \"height\": xxxx,\n"
            "    \"hash\": \"xxxx\",\n"
            "    \"branchlen\": 1          (numeric) length of branch connecting the tip to the main chain\n"
            "    \"status\": \"xxxx\"        (string) status of the chain (active, valid-fork, valid-headers, headers-only, invalid)\n"
            "  }\n"
            "]\n"
            "Possible values for status:\n"
            "1.  \"invalid\"               This branch contains at least one invalid block\n"
            "2.  \"headers-only\"          Not all blocks for this branch are available, but the headers are valid\n"
            "3.  \"valid-headers\"         All blocks are available for this branch, but they were never fully validated\n"
            "4.  \"valid-fork\"            This branch is not part of the active chain, but is fully validated\n"
            "5.  \"active\"                This is the tip of the active main chain, which is certainly valid\n"
            "\nExamples:\n"
            + HelpExampleCli("getchaintips", "")
            + HelpExampleRpc("getchaintips", "")
        );

    LOCK(cs_main);

    /*
     * Idea:  the set of chain tips is chainActive.tip, plus orphan blocks which do not have another orphan building off of them.
     * Algorithm:
     *  - Make one pass through mapBlockIndex, picking out the orphan blocks, and also storing a set of the orphan block's pprev pointers.
     *  - Iterate through the orphan blocks. If the block isn't pointed to by another orphan, it is a chain tip.
     *  - add chainActive.Tip()
     */
    std::set<const CBlockIndex*, CompareBlocksByHeight> setTips;
    std::set<const CBlockIndex*> setOrphans;
    std::set<const CBlockIndex*> setPrevs;

    for (const std::pair<const uint256, CBlockIndex*>& item : mapBlockIndex)
    {
        if (!chainActive.Contains(item.second)) {
            setOrphans.insert(item.second);
            setPrevs.insert(item.second->pprev);
        }
    }

    for (std::set<const CBlockIndex*>::iterator it = setOrphans.begin(); it != setOrphans.end(); ++it)
    {
        if (setPrevs.erase(*it) == 0) {
            setTips.insert(*it);
        }
    }

    // Always report the currently active tip.
    setTips.insert(chainActive.Tip());

    /* Construct the output array.  */
    UniValue res(UniValue::VARR);
    for (const CBlockIndex* block : setTips)
    {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("height", block->nHeight));
        obj.push_back(Pair("hash", block->phashBlock->GetHex()));

        const int branchLen = block->nHeight - chainActive.FindFork(block)->nHeight;
        obj.push_back(Pair("branchlen", branchLen));

        std::string status;
        if (chainActive.Contains(block)) {
            // This block is part of the currently active chain.
            status = "active";
        } else if (block->nStatus & BLOCK_FAILED_MASK) {
            // This block or one of its ancestors is invalid.
            status = "invalid";
        } else if (block->nChainTx == 0) {
            // This block cannot be connected because full block data for it or one of its parents is missing.
            status = "headers-only";
        } else if (block->IsValid(BLOCK_VALID_SCRIPTS)) {
            // This block is fully validated, but no longer part of the active chain. It was probably the active block once, but was reorganized.
            status = "valid-fork";
        } else if (block->IsValid(BLOCK_VALID_TREE)) {
            // The headers for this block are valid, but it has not been validated. It was probably never part of the most-work chain.
            status = "valid-headers";
        } else {
            // No clue.
            status = "unknown";
        }
        obj.push_back(Pair("status", status));

        res.push_back(obj);
    }

    return res;
}

UniValue mempoolInfoToJSON()
{
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("size", (int64_t) mempool.size()));
    ret.push_back(Pair("bytes", (int64_t) mempool.GetTotalTxSize()));
    ret.push_back(Pair("usage", (int64_t) mempool.DynamicMemoryUsage()));
    size_t maxmempool = gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    ret.push_back(Pair("maxmempool", (int64_t) maxmempool));
    ret.push_back(Pair("mempoolminfee", ValueFromAmount(std::max(mempool.GetMinFee(maxmempool), ::minRelayTxFee).GetFeePerK())));
    ret.push_back(Pair("minrelaytxfee", ValueFromAmount(::minRelayTxFee.GetFeePerK())));

    return ret;
}

UniValue getmempoolinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getmempoolinfo\n"
            "\nReturns details on the active state of the TX memory pool.\n"
            "\nResult:\n"
            "{\n"
            "  \"size\": xxxxx,               (numeric) Current tx count\n"
            "  \"bytes\": xxxxx,              (numeric) Sum of all virtual transaction sizes as defined in BIP 141. Differs from actual serialized size because witness data is discounted\n"
            "  \"usage\": xxxxx,              (numeric) Total memory usage for the mempool\n"
            "  \"maxmempool\": xxxxx,         (numeric) Maximum memory usage for the mempool\n"
            "  \"mempoolminfee\": xxxxx       (numeric) Minimum fee rate in " + CURRENCY_UNIT + "/kB for tx to be accepted. Is the maximum of minrelaytxfee and minimum mempool fee\n"
            "  \"minrelaytxfee\": xxxxx       (numeric) Current minimum relay fee for transactions\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolinfo", "")
            + HelpExampleRpc("getmempoolinfo", "")
        );

    return mempoolInfoToJSON();
}

UniValue preciousblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "preciousblock \"blockhash\"\n"
            "\nTreats a block as if it were received before others with the same work.\n"
            "\nA later preciousblock call can override the effect of an earlier one.\n"
            "\nThe effects of preciousblock are not retained across restarts.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to mark as precious\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("preciousblock", "\"blockhash\"")
            + HelpExampleRpc("preciousblock", "\"blockhash\"")
        );

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    CBlockIndex* pblockindex;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        pblockindex = mapBlockIndex[hash];
    }

    CValidationState state;
    PreciousBlock(state, Params(), pblockindex);

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

///// contract
UniValue getsimplecontractinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1)
        throw runtime_error(
                "getsimplecontractinfo \"addressOrName\" ( string )\n"
                        "\nArgument:\n"
                        "1. \"addressOrName\"          (string, required) The contract address or contract name\n"
        );

    LOCK(cs_main);

    std::string strAddr = request.params[0].get_str();
    auto service = get_contract_storage_service();
    service->open();
	::contract::storage::ContractInfoP contract_info;
	if (ContractHelper::is_valid_contract_address_format(strAddr)) {
		contract_info = service->get_contract_info(strAddr);
	} else {
		if (!ContractHelper::is_valid_contract_name_format(strAddr)) {
			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address does not exist");
		}
		auto contract_addr = service->find_contract_id_by_name(strAddr);
		if (contract_addr.empty())
			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Contract address or name does not exist");
		contract_info = service->get_contract_info(contract_addr);
	}
	if(!contract_info)
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Contract address or name does not exist");

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("id", contract_info->id));
    result.push_back(Pair("creator_address", contract_info->creator_address));
    result.push_back(Pair("name", contract_info->name));
    result.push_back(Pair("description", contract_info->description));
	result.push_back(Pair("txid", contract_info->txid));
	result.push_back(Pair("version", uint64_t(contract_info->version)));
	result.push_back(Pair("type", contract_info->is_native ? "native" : "bytecode"));
	result.push_back(Pair("template_name", contract_info->contract_template_key));
	{
		UniValue apis(UniValue::VARR);
		for (const auto& api : contract_info->apis) {
			UniValue item(UniValue::VOBJ);
			item.push_back(Pair("name", api));
			apis.push_back(item);
		}
		result.push_back(Pair("apis", apis));
	}
	{
		UniValue offline_apis(UniValue::VARR);
		for (const auto& api : contract_info->offline_apis) {
			UniValue item(UniValue::VOBJ);
			item.push_back(Pair("name", api));
			offline_apis.push_back(item);
		}
		result.push_back(Pair("offline_apis", offline_apis));
	}
	{
		UniValue storages(UniValue::VARR);
		for (const auto& p : contract_info->storage_types) {
			UniValue item(UniValue::VOBJ);
			item.push_back(Pair("name", p.first));
			item.push_back(Pair("type", (uint64_t)p.second));
			storages.push_back(item);
		}
		result.push_back(Pair("storages", storages));
	}
	{
		UniValue balances(UniValue::VARR);
		for (const auto& b : contract_info->balances) {
			UniValue item(UniValue::VOBJ);
			item.push_back(Pair("asset_id", uint64_t(b.asset_id)));
			item.push_back(Pair("amount", b.amount));
			balances.push_back(item);
		}
		result.push_back(Pair("balances", balances));
	}

    return result;
}

UniValue getcontractinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1)
        throw runtime_error(
                "getcontractinfo \"addressOrName\" ( string )\n"
                        "\nArgument:\n"
                        "1. \"addressOrName\"          (string, required) The contract address or name\n"
        );

    LOCK(cs_main);

    std::string strAddr = request.params[0].get_str();
    auto service = get_contract_storage_service();
    service->open();
	::contract::storage::ContractInfoP contract_info;
	if (ContractHelper::is_valid_contract_address_format(strAddr)) {
		contract_info = service->get_contract_info(strAddr);
	} else {
		if (!ContractHelper::is_valid_contract_name_format(strAddr)) {
			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address does not exist");
		}
		auto contract_addr = service->find_contract_id_by_name(strAddr);
		if (contract_addr.empty())
			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Contract address or name does not exist");
		contract_info = service->get_contract_info(contract_addr);
	}
	if (!contract_info)
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Contract address or name does not exist");

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("id", contract_info->id));
    result.push_back(Pair("creator_address", contract_info->creator_address));
    result.push_back(Pair("name", contract_info->name));
    result.push_back(Pair("description", contract_info->description));
	result.push_back(Pair("txid", contract_info->txid));
	result.push_back(Pair("version", uint64_t(contract_info->version)));
	result.push_back(Pair("type", contract_info->is_native ? "native" : "bytecode"));
	result.push_back(Pair("template_name", contract_info->contract_template_key));
	{
		UniValue apis(UniValue::VARR);
		for (const auto& api : contract_info->apis) {
			UniValue item(UniValue::VOBJ);
			item.push_back(Pair("name", api));
			apis.push_back(item);
		}
		result.push_back(Pair("apis", apis));
	}
	{
		UniValue offline_apis(UniValue::VARR);
		for (const auto& api : contract_info->offline_apis) {
			UniValue item(UniValue::VOBJ);
			item.push_back(Pair("name", api));
			offline_apis.push_back(item);
		}
		result.push_back(Pair("offline_apis", offline_apis));
	}
	auto bytecode_base64 = fjson::base64_encode(contract_info->bytecode.data(), contract_info->bytecode.size());
	result.push_back(Pair("code", bytecode_base64));
	{
		UniValue storages(UniValue::VARR);
		for (const auto& p : contract_info->storage_types) {
			UniValue item(UniValue::VOBJ);
			item.push_back(Pair("name", p.first));
			item.push_back(Pair("type", (uint64_t)p.second));
			storages.push_back(item);
		}
		result.push_back(Pair("storages", storages));
	}
	{
		UniValue balances(UniValue::VARR);
		for (const auto& b : contract_info->balances) {
			UniValue item(UniValue::VOBJ);
			item.push_back(Pair("asset_id", uint64_t(b.asset_id)));
			item.push_back(Pair("amount", b.amount));
			balances.push_back(item);
		}
		result.push_back(Pair("balances", balances));
	}

    return result;
}

UniValue gettransactionevents(const JSONRPCRequest& request)
{
	if (request.fHelp || request.params.size() < 1)
		throw runtime_error(
			"gettransactionevents \"txid\" ( string )\n"
			"\nArgument:\n"
			"1. \"txid\"          (string, required) The transaction id\n"
		);

	LOCK(cs_main);

	std::string txid = request.params[0].get_str();
	auto service = get_contract_storage_service();
	service->open();
	::contract::storage::ContractInfoP contract_info;
	
	auto events = service->get_transaction_events(txid);


	UniValue result(UniValue::VARR);
	for (auto i = 0; i < events->size(); i++) {
		const ::contract::storage::ContractEventInfo& event_info = (*events)[i];
		UniValue item(UniValue::VOBJ);
		item.push_back(Pair("txid", event_info.transaction_id));
		item.push_back(Pair("event_name", event_info.event_name));
		item.push_back(Pair("event_arg", event_info.event_arg));
		item.push_back(Pair("contract_address", event_info.contract_id));
		result.push_back(item);
	}
	
	return result;
}

UniValue currentrootstatehash(const JSONRPCRequest& request)
{
    LOCK(cs_main);
    auto service = get_contract_storage_service();
    const auto& root_state_hash = service->current_root_state_hash();
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("root_state_hash", root_state_hash));
    return result;
}

UniValue blockrootstatehash(const JSONRPCRequest& request)
{
	if (request.fHelp || request.params.size() < 1)
		throw runtime_error(
			"currentrootstatehash \"block_height\" ( int )\n"
			"\nArgument:\n"
			"1. \"block_height\"          (int, required) The block height to get root state hash\n"
		);

	LOCK(cs_main);
	int64_t height;
    try {
        height = boost::lexical_cast<int64_t>(request.params[0].getValStr());
    } catch(boost::bad_lexical_cast& e) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid block height");
    }
	if (height <= 0 || height > chainActive.Height())
		throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid block height");
	if (height < Params().GetConsensus().UBCONTRACT_Height)
		throw JSONRPCError(RPC_INVALID_PARAMETER, "contract feature is not allowed in this block height");
	auto bindex = chainActive.Tip()->GetAncestor(height);
	if (bindex->nHeight != height)
		throw JSONRPCError(RPC_INVALID_PARAMETER, "can't find valid block of this height");
	CBlock block;
	auto res = ReadBlockFromDisk(block, bindex, Params().GetConsensus());
	if (!res)
		throw JSONRPCError(RPC_INVALID_PARAMETER, "can't find valid block of this height");
	auto maybe_root_state_hash_in_block = get_root_state_hash_from_block(&block);
	auto root_state_hash = maybe_root_state_hash_in_block ? *maybe_root_state_hash_in_block : std::string(EMPTY_COMMIT_ID);
	UniValue result(UniValue::VOBJ);
	result.push_back(Pair("root_state_hash", root_state_hash));
	return result;
}

UniValue isrootstatehashnewer(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 0)
        throw runtime_error(
                "isrootstatehashnewer ( )\n"
                "\ncheck whether root current root state hash newer then root state hash in chainbestblock\n"
        );

    LOCK(cs_main);
    auto bindex = chainActive.Tip();
    CBlock block;
    auto res = ReadBlockFromDisk(block, bindex, Params().GetConsensus());
    if (!res)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "can't find valid block of this height");
    auto maybe_root_state_hash_in_block = get_root_state_hash_from_block(&block);
    auto bestblock_root_state_hash = maybe_root_state_hash_in_block ? *maybe_root_state_hash_in_block : std::string(EMPTY_COMMIT_ID);

    auto service = get_contract_storage_service();
    const auto& current_root_state_hash = service->current_root_state_hash();

    bool is_current_root_state_hash_after_best_block = service->is_current_root_state_hash_after(bestblock_root_state_hash);

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("current_root_state_hash", current_root_state_hash));
    result.push_back(Pair("best_block_root_state_hash", bestblock_root_state_hash));
    result.push_back(Pair("is_current_root_state_hash_after_best_block", is_current_root_state_hash_after_best_block));
    return result;
}

UniValue rollbackrootstatehash(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1)
        throw runtime_error(
                "rollbackrootstatehash \"to_rootstatehash\" ( int )\n"
                "This is a dangerous api.\nArgument:\n"
                "1. \"to_rootstatehash\"          (int, required) destination rootstatehash to rollback to\n"
        );

    LOCK(cs_main);
    auto to_rootstatehash = request.params[0].get_str();
	auto service = get_contract_storage_service();
	UniValue result(UniValue::VOBJ);
	try {
		const auto& current_root_state_hash = service->current_root_state_hash();
		if (to_rootstatehash == current_root_state_hash)
			throw JSONRPCError(RPC_INVALID_PARAMETER, "can't rollback to current root state hash");
		if (to_rootstatehash != EMPTY_COMMIT_ID) {
			const auto& commit_info = service->get_commit_info(to_rootstatehash);
			if (!commit_info)
				throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("can't find commit ") + to_rootstatehash);
		}
		service->rollback_contract_state(to_rootstatehash);
		
		result.push_back(Pair("current_root_state_hash", service->current_root_state_hash()));
	}
	catch (::contract::storage::ContractStorageException& e) {
		throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("contract storage error ") + e.what());
	}
	return result;
}


UniValue rollbacktoheight(const JSONRPCRequest& request)
{
    if (request.fHelp /*|| request.params.size() < 1*/)
        throw runtime_error(
                "rollbacktoheight \"null"
                "This is a dangerous api.\nArgument:\n"
                //"1. \"height\"          (int, required) destination blockchain height to rollback to\n"
        );

    LOCK(cs_main);
    //auto to_height = request.params[0].get_str();
	
	auto latestHeight = chainActive.Height();
	bool result = false;
	//LogPrintf("latestHeight: %d,mapBlockIndex.size:%d\n", latestHeight,mapBlockIndex.size());
	for (auto it = mapBlockIndex.begin(); it != mapBlockIndex.end(); it++) {
		auto bindex = it->second;
		//LogPrintf("before reset,bindex->nHeight: %d,bindex->nStatus:%d\n", bindex->nHeight,bindex->nStatus);
		if (bindex && bindex->nHeight > latestHeight && (bindex->nStatus & BLOCK_FAILED_MASK)) {
		    //LogPrintf("bindex->nHeight: %d,bindex->nStatus:%d\n", bindex->nHeight,bindex->nStatus);
			result = ResetBlockFailureFlags(bindex);
		}
	}

	return result;
}


UniValue getcontractstorage(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2)
        throw runtime_error(
                "getcontractstorage \"contract_address\" \"storage_name\" ( string, string )\n"
                "\nArgument:\n"
                "1. \"contract_address\"          (string, required) The contract address to get contract storage\n"
                "2. \"storage_name\"              (string, required) The storage name to query\n"
        );

    LOCK(cs_main);
    const auto& contract_address = request.params[0].get_str();
    const auto& storage_name = request.params[1].get_str();
    if(!ContractHelper::is_valid_contract_address_format(contract_address)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid contract address");
    }
    if (storage_name.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid storage name");

    auto service = get_contract_storage_service();
    const auto& storage_value = service->get_contract_storage(contract_address, storage_name);
    const auto& storage_value_json = jsondiff::json_dumps(storage_value);
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("value", storage_value_json));
    return result;
}

UniValue getcreatecontractaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1)
        throw runtime_error(
                "getcreatecontractaddress \"tx\" ( tx )\n"
                        "\nArgument:\n"
                        "1. \"tx\"          (string, required) The contract tx hex string\n"
        );

    LOCK(cs_main);
    RPCTypeCheck(request.params, { UniValue::VSTR }, true);

    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str(), true))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        for (const CTxIn& txin : mtx.vin) {
            view.AccessCoin(txin.prevout); // Load entries from viewChain into view; can fail.
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    // Script verification errors
    UniValue vErrors(UniValue::VARR);
    std::string contract_address;
    std::vector<std::string> contract_addresses;
    UniValue addresses_results(UniValue::VARR);

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mtx);
    if(txConst.HasContractOp() && !txConst.HasOpSpend()) {
        CBlock block;
        block.vtx.push_back(MakeTransactionRef(txConst));
        CCoinsViewCache &view = *pcoinsTip;
        ContractTxConverter converter(txConst, &view, &block.vtx, true);
        ExtractContractTX resultConvertContractTx;
        std::string error_ret;
        if (!converter.extractionContractTransactions(resultConvertContractTx, error_ret)) {
            throw runtime_error(std::string("decode contract tx failed.") + error_ret);
        }
        if(resultConvertContractTx.txs.size() < 1)
        {
            throw runtime_error("decode contract tx failed, empty contract txs");
        }
        else
        {
            for(const auto& con_tx : resultConvertContractTx.txs) {
                if ((OP_CREATE == con_tx.opcode) || (OP_CREATE_NATIVE == con_tx.opcode)) {
                    contract_addresses.push_back(con_tx.params.contract_address);
                    UniValue entry(UniValue::VOBJ);
                    entry.push_back(Pair("address", con_tx.params.contract_address));
                    addresses_results.push_back(entry);
                    // contract_address = con_tx.params.contract_address;
                } else {
                    throw runtime_error("this is not create contract transaction");
                }
            }
            if(contract_addresses.size() > 0) {
                contract_address = contract_addresses[0];
            }
        }
    } else {
        throw runtime_error("this is not create contract transaction");
    }

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("address", contract_address));
    result.push_back(Pair("addresses", addresses_results));
    return result;
}

static const uint64_t testing_invoke_contract_gas_limit = 100000000;

UniValue invokecontractoffline(const JSONRPCRequest& request)
{
	if (request.fHelp || request.params.size() < 4)
		throw runtime_error(
			"invokecontractoffline \"caller_address\" \"contract_address\" \"api_name\" \"api_arg\" ( caller_address, contract_address, api_name, api_arg )\n"
			"\nArgument:\n"
			"1. \"caller_address\"            (string, required) The caller address\n"
			"2. \"contract_address\"          (string, required) The contract address\n"
			"3. \"api_name\" (string, required) The contract api name to be invoked\n"
			"4. \"api_arg\" (string, required) The contract api argument\n"
		);

	LOCK(cs_main);
	std::string caller_address = request.params[0].get_str();
	if (caller_address.length()<20) // FIXME
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect address");
    // TODO: check whether has secret of caller_address
	std::string contract_address = request.params[1].get_str();
	if (!ContractHelper::is_valid_contract_address_format(contract_address))
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect address");
	std::string api_name = request.params[2].get_str();
	if(api_name.empty())
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Incorrect contract api name");
	std::string api_arg = request.params[3].get_str();

    auto service = get_contract_storage_service();
	service->open();

	const auto& old_root_state_hash = service->current_root_state_hash();

	auto contract_info = service->get_contract_info(contract_address);
	if (!contract_info)
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address does not exist");

	BOOST_SCOPE_EXIT_ALL(&service, old_root_state_hash) {
		service->open();
		service->rollback_contract_state(old_root_state_hash);
		service->close();
	};

	CBlock block;
	CMutableTransaction tx;
	uint64_t gas_limit = testing_invoke_contract_gas_limit;
	uint64_t gas_price = 40;

	valtype version;
	version.push_back(0x01);
	tx.vout.push_back(CTxOut(0, CScript() << version << ToByteVector(api_arg) << ToByteVector(api_name) << ToByteVector(contract_address) << ToByteVector(caller_address) << gas_limit << gas_price << OP_CALL));
	block.vtx.push_back(MakeTransactionRef(CTransaction(tx)));

	std::vector<ContractTransaction> contractTransactions;
	ContractTransaction contract_tx;
	contract_tx.opcode = OP_CALL;
	contract_tx.params.caller_address = caller_address;
	contract_tx.params.caller = "";
	contract_tx.params.api_name = api_name;
	contract_tx.params.api_arg = api_arg;
	contract_tx.params.contract_address = contract_address;
	contract_tx.params.gasPrice = gas_price;
	contract_tx.params.gasLimit = gas_limit;
	contract_tx.params.version = CONTRACT_MAJOR_VERSION;
	contractTransactions.push_back(contract_tx);

	ContractExec exec(service.get(), block, contractTransactions, gas_limit, 0);
	if (!exec.performByteCode()) {
		//error, don't add contract
        throw JSONRPCError(RPC_INTERNAL_ERROR, exec.pending_contract_exec_result.error_message);
	}
	ContractExecResult execResult;
	if (!exec.processingResults(execResult)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "process exec result error");
	}

	UniValue result(UniValue::VOBJ);
	result.push_back(Pair("result", execResult.api_result));
	result.push_back(Pair("gasCount", execResult.usedGas));
	// balance changes
	UniValue balance_changes(UniValue::VARR);
	for (auto i = 0; i < execResult.balance_changes.size(); i++) {
		const auto& change = execResult.balance_changes[i];
		UniValue item(UniValue::VOBJ);
		item.push_back(Pair("is_contract", change.is_contract));
		item.push_back(Pair("address", change.address));
		item.push_back(Pair("is_add", change.add));
		item.push_back(Pair("amount", change.amount));
		balance_changes.push_back(item);
	}
	result.push_back(Pair("balanceChanges", balance_changes));
	
	return result;
}

UniValue registercontracttesting(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2)
        throw runtime_error(
                "registercontracttesting \"caller_address\" \"bytecode_hex\" ( caller_address, bytecode_hex )\n"
                        "\nArgument:\n"
                        "1. \"caller_address\"            (string, required) The caller address\n"
                        "2. \"bytecode_hex\"          (string, required) The contract bytecode hex\n"
        );

    LOCK(cs_main);
    std::string caller_address = request.params[0].get_str();
    if (caller_address.length()<20) // FIXME
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect address");
    // TODO: check whether has secret of caller_address
    const auto& bytecode_hex = request.params[1].get_str();

    std::vector<unsigned char> contract_data = ParseHexV(bytecode_hex,"Data");

    auto service = get_contract_storage_service();
    service->open();

    const auto& old_root_state_hash = service->current_root_state_hash();


    BOOST_SCOPE_EXIT_ALL(&service, old_root_state_hash) {
        service->open();
        service->rollback_contract_state(old_root_state_hash);
        service->close();
    };

    CBlock block;
    CMutableTransaction tx;
    uint64_t gas_limit = testing_invoke_contract_gas_limit;
    uint64_t gas_price = 40;

    valtype version;
    version.push_back(0x01);
    tx.vout.push_back(CTxOut(0, CScript() << version << contract_data << ToByteVector(caller_address) << gas_limit << gas_price << OP_CREATE));
    block.vtx.push_back(MakeTransactionRef(CTransaction(tx)));

    std::vector<ContractTransaction> contractTransactions;
    ContractTransaction contract_tx;
    contract_tx.opcode = OP_CREATE;
    contract_tx.params.caller_address = caller_address;
    contract_tx.params.caller = "";
    contract_tx.params.api_name = "";
    contract_tx.params.api_arg = "";
    contract_tx.params.gasPrice = gas_price;
    contract_tx.params.gasLimit = gas_limit;
    contract_tx.params.code = ContractHelper::load_contract_from_gpc_data(contract_data);
	contract_tx.params.contract_address = "local_contract";
    contract_tx.params.version = CONTRACT_MAJOR_VERSION;
    contractTransactions.push_back(contract_tx);

    ContractExec exec(service.get(), block, contractTransactions, gas_limit, 0);
    if (!exec.performByteCode()) {
        //error, don't add contract
        return false;
    }
    ContractExecResult execResult;
    if (!exec.processingResults(execResult)) {
        return false;
    }

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("gasCount", execResult.usedGas));
	// balance changes
	UniValue balance_changes(UniValue::VARR);
	for (auto i = 0; i < execResult.balance_changes.size(); i++) {
		const auto& change = execResult.balance_changes[i];
		UniValue item(UniValue::VOBJ);
		item.push_back(Pair("is_contract", change.is_contract));
		item.push_back(Pair("address", change.address));
		item.push_back(Pair("is_add", change.add));
		item.push_back(Pair("amount", change.amount));
		balance_changes.push_back(item);
	}
	result.push_back(Pair("balanceChanges", balance_changes));
    return result;
}

UniValue registernativecontracttesting(const JSONRPCRequest& request)
{
	if (request.fHelp || request.params.size() < 2)
		throw runtime_error(
			"registernativecontracttesting \"caller_address\" \"template_name\" ( caller_address, bytecode_hex )\n"
			"\nArgument:\n"
			"1. \"caller_address\"            (string, required) The caller address\n"
			"2. \"template_name\"          (string, required) The contract template name\n"
		);

	LOCK(cs_main);
	std::string caller_address = request.params[0].get_str();
	if (caller_address.length()<20) // FIXME
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect address");
	// TODO: check whether has secret of caller_address
	const auto& template_name = request.params[1].get_str();
	if(!blockchain::contract::native_contract_finder::has_native_contract_with_key(template_name))
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Incorrect native contract template name");

	auto service = get_contract_storage_service();
	service->open();

	const auto& old_root_state_hash = service->current_root_state_hash();


	BOOST_SCOPE_EXIT_ALL(&service, old_root_state_hash) {
		service->open();
		service->rollback_contract_state(old_root_state_hash);
		service->close();
	};

	CBlock block;
	CMutableTransaction tx;
	uint64_t gas_limit = testing_invoke_contract_gas_limit;
	uint64_t gas_price = 40;

	valtype version;
	version.push_back(0x01);
	tx.vout.push_back(CTxOut(0, CScript() << version << ToByteVector(template_name) << ToByteVector(caller_address) << gas_limit << gas_price << OP_CREATE_NATIVE));
	block.vtx.push_back(MakeTransactionRef(CTransaction(tx)));

	std::vector<ContractTransaction> contractTransactions;
	ContractTransaction contract_tx;
	contract_tx.opcode = OP_CREATE_NATIVE;
	contract_tx.params.caller_address = caller_address;
	contract_tx.params.caller = "";
	contract_tx.params.api_name = "";
	contract_tx.params.api_arg = "";
	contract_tx.params.gasPrice = gas_price;
	contract_tx.params.gasLimit = gas_limit;
	contract_tx.params.is_native = true;
	contract_tx.params.template_name = template_name;
	contract_tx.params.contract_address = "local_contract";
	contract_tx.params.version = CONTRACT_MAJOR_VERSION;
	contractTransactions.push_back(contract_tx);

	ContractExec exec(service.get(), block, contractTransactions, gas_limit, 0);
	if (!exec.performByteCode()) {
		//error, don't add contract
		return false;
	}
	ContractExecResult execResult;
	if (!exec.processingResults(execResult)) {
		return false;
	}

	UniValue result(UniValue::VOBJ);
	result.push_back(Pair("gasCount", execResult.usedGas));
	// balance changes
	UniValue balance_changes(UniValue::VARR);
	for (auto i = 0; i < execResult.balance_changes.size(); i++) {
		const auto& change = execResult.balance_changes[i];
		UniValue item(UniValue::VOBJ);
		item.push_back(Pair("is_contract", change.is_contract));
		item.push_back(Pair("address", change.address));
		item.push_back(Pair("is_add", change.add));
		item.push_back(Pair("amount", change.amount));
		balance_changes.push_back(item);
	}
	result.push_back(Pair("balanceChanges", balance_changes));
	return result;
}

UniValue upgradecontracttesting(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 4)
        throw runtime_error(
                "upgradecontracttesting \"caller_address\" \"contract address\" \"new contract name\" \"contract description\"\n"
                        "\nArgument:\n"
                        "1. \"caller_address\"            (string, required) The caller address\n"
                        "2. \"contract address\"          (string, required) The contract address to upgrade\n"
                        "3. \"new contract name\"         (string, required) The new contract name\n"
                        "4. \"contract description\"         (string, required) The contract description\n"

        );

    LOCK(cs_main);
    const auto& caller_address = request.params[0].get_str();
    if (caller_address.length()<20) // FIXME
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect address");
    // TODO: check whether has secret of caller_address
    const auto& contract_address = request.params[1].get_str();
    if(!ContractHelper::is_valid_contract_address_format(contract_address))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect contract address");
    const auto& contract_name = request.params[2].get_str();
    if(!ContractHelper::is_valid_contract_name_format(contract_name))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Incorrect contract name format");
    const auto& contract_desc = request.params[3].get_str();
    if(!ContractHelper::is_valid_contract_desc_format(contract_desc))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Incorrect contract description format");

    auto service = get_contract_storage_service();
    service->open();
    const auto& old_root_state_hash = service->current_root_state_hash();
    BOOST_SCOPE_EXIT_ALL(&service, old_root_state_hash) {
        service->open();
        service->rollback_contract_state(old_root_state_hash);
        service->close();
    };

    CBlock block;
    CMutableTransaction tx;
    uint64_t gas_limit = testing_invoke_contract_gas_limit;
    uint64_t gas_price = 40;

    valtype version;
    version.push_back(0x01);
    tx.vout.push_back(CTxOut(0, CScript() << version << ToByteVector(contract_desc) << ToByteVector(contract_name) << ToByteVector(contract_address) << ToByteVector(caller_address) << gas_limit << gas_price << OP_UPGRADE));
    block.vtx.push_back(MakeTransactionRef(CTransaction(tx)));

    std::vector<ContractTransaction> contractTransactions;
    ContractTransaction contract_tx;
    contract_tx.opcode = OP_UPGRADE;
    contract_tx.params.caller_address = caller_address;
    contract_tx.params.caller = "";
    contract_tx.params.api_name = "";
    contract_tx.params.api_arg = "";
    contract_tx.params.gasPrice = gas_price;
    contract_tx.params.gasLimit = gas_limit;
    contract_tx.params.contract_address = contract_address;
    contract_tx.params.contract_name = contract_name;
    contract_tx.params.contract_desc = contract_desc;
    contract_tx.params.version = CONTRACT_MAJOR_VERSION;
    contractTransactions.push_back(contract_tx);

    ContractExec exec(service.get(), block, contractTransactions, gas_limit, 0);
    if (!exec.performByteCode()) {
        //error, don't add contract
        return false;
    }
    ContractExecResult execResult;
    if (!exec.processingResults(execResult)) {
        return false;
    }

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("gasCount", execResult.usedGas));
	// balance changes
	UniValue balance_changes(UniValue::VARR);
	for (auto i = 0; i < execResult.balance_changes.size(); i++) {
		const auto& change = execResult.balance_changes[i];
		UniValue item(UniValue::VOBJ);
		item.push_back(Pair("is_contract", change.is_contract));
		item.push_back(Pair("address", change.address));
		item.push_back(Pair("is_add", change.add));
		item.push_back(Pair("amount", change.amount));
		balance_changes.push_back(item);
	}
	result.push_back(Pair("balanceChanges", balance_changes));
    return result;
}

UniValue deposittocontracttesting(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 4)
        throw runtime_error(
                "deposittocontracttesting \"caller_address\" \"contract address\" \"deposit amount with precision(int)\" \"deposit memo\"\n"
                        "\nArgument:\n"
                        "1. \"caller_address\"            (string, required) The caller address\n"
                        "2. \"contract address\"          (string, required) The contract address to upgrade\n"
                        "3. \"deposit amount with precision(int)\"         (int, required) deposit amount with precision(int)\n"
                        "4. \"deposit memo\"         (string, required) The deposit memo\n"

        );

    LOCK(cs_main);
    const auto& caller_address = request.params[0].get_str();
    if (caller_address.length()<20) // FIXME
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect address");
    // TODO: check whether has secret of caller_address
    const auto& contract_address = request.params[1].get_str();
    if(!ContractHelper::is_valid_contract_address_format(contract_address))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect contract address");
    auto deposit_amount = request.params[2].get_int64();
    if(deposit_amount<=0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Incorrect deposit amount");
    const auto& memo = request.params[3].get_str();

    auto service = get_contract_storage_service();
    service->open();
    const auto& old_root_state_hash = service->current_root_state_hash();
    BOOST_SCOPE_EXIT_ALL(&service, old_root_state_hash) {
        service->open();
        service->rollback_contract_state(old_root_state_hash);
        service->close();
    };

    CBlock block;
    CMutableTransaction tx;
    uint64_t gas_limit = testing_invoke_contract_gas_limit;
    uint64_t gas_price = 40;

    valtype version;
    version.push_back(0x01);
    tx.vout.push_back(CTxOut(0, CScript() << version << ToByteVector(memo) << deposit_amount << ToByteVector(contract_address) << ToByteVector(caller_address) << gas_limit << gas_price << OP_DEPOSIT_TO_CONTRACT));
    block.vtx.push_back(MakeTransactionRef(CTransaction(tx)));

    std::vector<ContractTransaction> contractTransactions;
    ContractTransaction contract_tx;
    contract_tx.opcode = OP_DEPOSIT_TO_CONTRACT;
    contract_tx.params.caller_address = caller_address;
    contract_tx.params.caller = "";
    contract_tx.params.api_name = "";
    contract_tx.params.api_arg = "";
    contract_tx.params.gasPrice = gas_price;
    contract_tx.params.gasLimit = gas_limit;
    contract_tx.params.contract_address = contract_address;
    contract_tx.params.deposit_amount = deposit_amount;
    contract_tx.params.deposit_memo = memo;
    contract_tx.params.version = CONTRACT_MAJOR_VERSION;
    contractTransactions.push_back(contract_tx);

    ContractExec exec(service.get(), block, contractTransactions, gas_limit, 0);
    if (!exec.performByteCode()) {
        //error, don't add contract
        return false;
    }
    ContractExecResult execResult;
    if (!exec.processingResults(execResult)) {
        return false;
    }

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("gasCount", execResult.usedGas));
	// balance changes
	UniValue balance_changes(UniValue::VARR);
	for (auto i = 0; i < execResult.balance_changes.size(); i++) {
		const auto& change = execResult.balance_changes[i];
		UniValue item(UniValue::VOBJ);
		item.push_back(Pair("is_contract", change.is_contract));
		item.push_back(Pair("address", change.address));
		item.push_back(Pair("is_add", change.add));
		item.push_back(Pair("amount", change.amount));
		balance_changes.push_back(item);
	}
	result.push_back(Pair("balanceChanges", balance_changes));
    return result;
}

///// end contract code

UniValue invalidateblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "invalidateblock \"blockhash\"\n"
            "\nPermanently marks a block as invalid, as if it violated a consensus rule.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to mark as invalid\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("invalidateblock", "\"blockhash\"")
            + HelpExampleRpc("invalidateblock", "\"blockhash\"")
        );

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        InvalidateBlock(state, Params(), pblockindex);
    }

    if (state.IsValid()) {
        ActivateBestChain(state, Params());
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue reconsiderblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "reconsiderblock \"blockhash\"\n"
            "\nRemoves invalidity status of a block and its descendants, reconsider them for activation.\n"
            "This can be used to undo the effects of invalidateblock.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to reconsider\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("reconsiderblock", "\"blockhash\"")
            + HelpExampleRpc("reconsiderblock", "\"blockhash\"")
        );

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        ResetBlockFailureFlags(pblockindex);
    }

    CValidationState state;
    ActivateBestChain(state, Params());

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue getchaintxstats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "getchaintxstats ( nblocks blockhash )\n"
            "\nCompute statistics about the total number and rate of transactions in the chain.\n"
            "\nArguments:\n"
            "1. nblocks      (numeric, optional) Size of the window in number of blocks (default: one month).\n"
            "2. \"blockhash\"  (string, optional) The hash of the block that ends the window.\n"
            "\nResult:\n"
            "{\n"
            "  \"time\": xxxxx,                (numeric) The timestamp for the final block in the window in UNIX format.\n"
            "  \"txcount\": xxxxx,             (numeric) The total number of transactions in the chain up to that point.\n"
            "  \"window_block_count\": xxxxx,  (numeric) Size of the window in number of blocks.\n"
            "  \"window_tx_count\": xxxxx,     (numeric) The number of transactions in the window. Only returned if \"window_block_count\" is > 0.\n"
            "  \"window_interval\": xxxxx,     (numeric) The elapsed time in the window in seconds. Only returned if \"window_block_count\" is > 0.\n"
            "  \"txrate\": x.xx,               (numeric) The average rate of transactions per second in the window. Only returned if \"window_interval\" is > 0.\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getchaintxstats", "")
            + HelpExampleRpc("getchaintxstats", "2016")
        );

    const CBlockIndex* pindex;
    int blockcount = 30 * 24 * 60 * 60 / Params().GetConsensus().nPowTargetSpacing; // By default: 1 month

    bool havehash = !request.params[1].isNull();
    uint256 hash;
    if (havehash) {
        hash = uint256S(request.params[1].get_str());
    }

    {
        LOCK(cs_main);
        if (havehash) {
            auto it = mapBlockIndex.find(hash);
            if (it == mapBlockIndex.end()) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
            }
            pindex = it->second;
            if (!chainActive.Contains(pindex)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Block is not in main chain");
            }
        } else {
            pindex = chainActive.Tip();
        }
    }
    
    assert(pindex != nullptr);

    if (request.params[0].isNull()) {
        blockcount = std::max(0, std::min(blockcount, pindex->nHeight - 1));
    } else {
        blockcount = request.params[0].get_int();

        if (blockcount < 0 || (blockcount > 0 && blockcount >= pindex->nHeight)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block count: should be between 0 and the block's height - 1");
        }
    }

    const CBlockIndex* pindexPast = pindex->GetAncestor(pindex->nHeight - blockcount);
    int nTimeDiff = pindex->GetMedianTimePast() - pindexPast->GetMedianTimePast();
    int nTxDiff = pindex->nChainTx - pindexPast->nChainTx;

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("time", (int64_t)pindex->nTime));
    ret.push_back(Pair("txcount", (int64_t)pindex->nChainTx));
    ret.push_back(Pair("window_block_count", blockcount));
    if (blockcount > 0) {
        ret.push_back(Pair("window_tx_count", nTxDiff));
        ret.push_back(Pair("window_interval", nTimeDiff));
        if (nTimeDiff > 0) {
            ret.push_back(Pair("txrate", ((double)nTxDiff) / nTimeDiff));
        }
    }

    return ret;
}

UniValue savemempool(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "savemempool\n"
            "\nDumps the mempool to disk.\n"
            "\nExamples:\n"
            + HelpExampleCli("savemempool", "")
            + HelpExampleRpc("savemempool", "")
        );
    }

    if (!DumpMempool()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Unable to dump mempool to disk");
    }

    return NullUniValue;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "blockchain",         "getblockchaininfo",      &getblockchaininfo,      {} },
    { "blockchain",         "getchaintxstats",        &getchaintxstats,        {"nblocks", "blockhash"} },
    { "blockchain",         "getbestblockhash",       &getbestblockhash,       {} },
    { "blockchain",         "getblockcount",          &getblockcount,          {} },
    { "blockchain",         "getblock",               &getblock,               {"blockhash","verbosity|verbose"} },
    { "blockchain",         "getblockhash",           &getblockhash,           {"height"} },
    { "blockchain",         "getblockheader",         &getblockheader,         {"blockhash","verbose"} },
    { "blockchain",         "getchaintips",           &getchaintips,           {} },
    { "blockchain",         "getdifficulty",          &getdifficulty,          {} },
    { "blockchain",         "getpowdifficulty",          &getpowdifficulty,    {} },
    { "blockchain",         "getposdifficulty",          &getposdifficulty,    {} },
    { "blockchain",         "getmempoolancestors",    &getmempoolancestors,    {"txid","verbose"} },
    { "blockchain",         "getmempooldescendants",  &getmempooldescendants,  {"txid","verbose"} },
    { "blockchain",         "getmempoolentry",        &getmempoolentry,        {"txid"} },
    { "blockchain",         "getmempoolinfo",         &getmempoolinfo,         {} },
    { "blockchain",         "getrawmempool",          &getrawmempool,          {"verbose"} },
    { "blockchain",         "gettxout",               &gettxout,               {"txid","n","include_mempool"} },
    { "blockchain",         "gettxoutsetinfo",        &gettxoutsetinfo,        {} },
  	{ "blockchain",         "getbalancetopn",         &getbalancetopn,         {"topn"} },
    { "blockchain",         "pruneblockchain",        &pruneblockchain,        {"height"} },
    { "blockchain",         "savemempool",            &savemempool,            {} },
    { "blockchain",         "verifychain",            &verifychain,            {"checklevel","nblocks"} },
    { "blockchain",         "getwhitelist",           &getwhitelist,           {"blocknum"} },
    { "blockchain",         "readwhitelist",           &readwhitelist,           {"filename"} },
    { "blockchain",         "preciousblock",          &preciousblock,          {"blockhash"} },

    { "blockchain",         "getcontractinfo",        &getcontractinfo,        {"contract_address"} },
	{ "blockchain",         "getsimplecontractinfo",  &getsimplecontractinfo,{ "contract_address" } },
	{ "blockchain",         "gettransactionevents",   &gettransactionevents,   {"txid"} },
    { "blockchain",         "getcreatecontractaddress", &getcreatecontractaddress, {"contact_tx"} },
	{ "blockchain",         "invokecontractoffline",  &invokecontractoffline,  {"caller_address", "contract_address", "api_name", "api_arg"} },
    { "blockchain",         "registercontracttesting",  &registercontracttesting,  {"caller_address", "bytecode_hex"} },
    { "blockchain",         "registernativecontracttesting",  &registernativecontracttesting,  {"caller_address", "contract_template_name"} },
    { "blockchain",         "upgradecontracttesting",  &upgradecontracttesting, {"caller_address", "contract_address", "contract_name", "contract_desc"} },
    { "blockchain",         "deposittocontracttesting", &deposittocontracttesting, {"caller_address", "contract_address", "deposit_amount", "deposit_memo"} },

    { "blockchain",         "currentrootstatehash", &currentrootstatehash, {} },
	{ "blockchain",         "blockrootstatehash", &blockrootstatehash,{"block_height"} },
    { "blockchain",         "isrootstatehashnewer", &isrootstatehashnewer,{} },
    { "blockchain",         "rollbackrootstatehash", &rollbackrootstatehash,{"to_rootstatehash"} },
    { "blockchain",         "rollbacktoheight", &rollbacktoheight,{"to_height"} },

    { "blockchain",         "getcontractstorage", &getcontractstorage, {} },

    /* Not shown in help */
    { "hidden",             "invalidateblock",        &invalidateblock,        {"blockhash"} },
    { "hidden",             "reconsiderblock",        &reconsiderblock,        {"blockhash"} },
    { "hidden",             "waitfornewblock",        &waitfornewblock,        {"timeout"} },
    { "hidden",             "waitforblock",           &waitforblock,           {"blockhash","timeout"} },
    { "hidden",             "waitforblockheight",     &waitforblockheight,     {"height","timeout"} },
    { "hidden",             "syncwithvalidationinterfacequeue", &syncwithvalidationinterfacequeue, {} },
};

void RegisterBlockchainRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
