#!/usr/bin/env python3
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.mininode import CTransaction, NetworkThread
from test_framework.blocktools import create_coinbase, create_block, add_witness_commitment
from test_framework.script import CScript, CScriptNum, read_contract_bytecode_hex, OP_CREATE, OP_CREATE_NATIVE, OP_CALL, OP_SPEND, \
    OP_DEPOSIT_TO_CONTRACT, OP_UPGRADE
from io import BytesIO
import time
import os
import sys
import random
import json
from decimal import Decimal
from test_framework import ub_utils
import pdb

config = {
    'PRECISION': 100000000,
    'CONTRACT_VERSION': b'\x01',
}


def get_address_balance(node, addr):
    utxos = node.listunspent()
    sum = Decimal(0)
    for utxo in utxos:
        if utxo.get('address', None) == addr:
            sum += Decimal(utxo.get('amount', 0))
    return sum


using_utxos = []


def get_utxo(node, caller_addr):
    utxos = node.listunspent()
    having_amount_items = list(filter(
        lambda x: x.get('address', None) == caller_addr and float(x.get('amount')) > 0.5 and float(
            x.get('amount')) <= 50,
        utxos))

    def in_using(item):
        for utxo in using_utxos:
            if utxo['txid'] == item['txid'] and utxo['vout'] == item['vout']:
                return True
        return False

    not_used_items = list(filter(lambda x: not in_using(x), having_amount_items))
    not_used_items = sorted(not_used_items, key = lambda x : x.get('amount'), reverse=True)
    utxo = not_used_items[0]
    using_utxos.append(utxo)
    return utxo


def testing_then_invoke_contract_api(node, caller_addr, contract_addr, api_name, api_arg):
    testing_res = node.invokecontractoffline(caller_addr, contract_addr, api_name, api_arg)
    print(testing_res)
    withdraw_from_infos = {}
    for change in testing_res['balanceChanges']:
        if change["is_contract"] and not change["is_add"]:
            withdraw_from_infos[change["address"]] = change["amount"] * 1.0 / config["PRECISION"]
    withdraw_infos = {}
    for change in testing_res["balanceChanges"]:
        if not change["is_contract"] and change["is_add"]:
            withdraw_infos[change["address"]] = change["amount"] * 1.0 / config["PRECISION"]
    return invoke_contract_api(node, caller_addr, contract_addr, api_name, api_arg,
                               withdraw_infos, withdraw_from_infos)


def generate_block(node, miner, count=1):
    res = node.generatetoaddress(count, miner, 5000000)
    if len(res)<count:
        print("mine res error: ", res)
    return res

def create_new_contract(node, caller_addr, contract_bytecode_path):
    utxo = get_utxo(node, caller_addr)
    bytecode_hex = read_contract_bytecode_hex(contract_bytecode_path)
    register_contract_script = CScript(
        [config['CONTRACT_VERSION'], bytes().fromhex(bytecode_hex), caller_addr.encode('utf8'), 10000,
         10, OP_CREATE])
    create_contract_script = register_contract_script.hex()
    create_contract_raw_tx = node.createrawtransaction([
        {
            'txid': utxo['txid'],
            'vout': utxo['vout'],
        },
    ],
        {
            caller_addr: '%.6f' % float(Decimal(utxo['amount']) - Decimal(0.01)),
            'contract': create_contract_script,
        })
    print("utxo: ", utxo)
    signed_create_contract_raw_tx_res = node.signrawtransaction(
        create_contract_raw_tx,
        [
            {
                'txid': utxo['txid'],
                'vout': utxo['vout'],
                'amount': utxo['amount'],
                'scriptPubKey': utxo['scriptPubKey'],
                'redeemScript': utxo.get('redeemScript', None),
            },
        ]
    )
    assert (signed_create_contract_raw_tx_res.get('complete', None) is True)
    signed_create_contract_raw_tx = signed_create_contract_raw_tx_res.get('hex')
    decoded_tx = node.decoderawtransaction(signed_create_contract_raw_tx)
    print("decoded_tx: ", decoded_tx)
    print("signed_create_contract_raw_tx: ", signed_create_contract_raw_tx)
    node.sendrawtransaction(signed_create_contract_raw_tx)
    contract_addr = node.getcreatecontractaddress(
        signed_create_contract_raw_tx
    )
    return contract_addr['address']


def create_new_native_contract(node, caller_addr, template_name):
    utxo = get_utxo(node, caller_addr)
    register_contract_script = CScript(
        [config['CONTRACT_VERSION'], template_name.encode('utf8'), caller_addr.encode('utf8'), 5000,
         10, OP_CREATE_NATIVE])
    create_contract_script = register_contract_script.hex()
    create_contract_raw_tx = node.createrawtransaction(
        [
            {
                'txid': utxo['txid'],
                'vout': utxo['vout'],
            },
        ],
        {
            caller_addr: '%.6f' % float(Decimal(utxo['amount']) - Decimal(0.01)),
            'contract': create_contract_script,
        },
    )
    signed_create_contract_raw_tx_res = node.signrawtransaction(
        create_contract_raw_tx,
        [
            {
                'txid': utxo['txid'],
                'vout': utxo['vout'],
                'amount': utxo['amount'],
                'scriptPubKey': utxo['scriptPubKey'],
                'redeemScript': utxo.get('redeemScript', None),
            },
        ],
    )
    assert (signed_create_contract_raw_tx_res.get('complete', None) is True)
    signed_create_contract_raw_tx = signed_create_contract_raw_tx_res.get('hex')
    print("nativecontract signed_create_contract_raw_tx: ", signed_create_contract_raw_tx)
    node.sendrawtransaction(signed_create_contract_raw_tx)
    contract_addr = node.getcreatecontractaddress(
        signed_create_contract_raw_tx
    )
    return contract_addr['address']


def upgrade_contract(node, caller_addr, contract_addr, contract_name, contract_desc):
    utxo = get_utxo(node, caller_addr)
    call_contract_script = CScript(
        [config['CONTRACT_VERSION'], contract_desc.encode("utf8"), contract_name.encode("utf8"),
         contract_addr.encode("utf8"),
         caller_addr.encode('utf8'),
         5000, 10, OP_UPGRADE])
    call_contract_script_hex = call_contract_script.hex()
    fee = 0.01
    call_contract_raw_tx = node.createrawtransaction(
        [
            {
                'txid': utxo['txid'],
                'vout': utxo['vout'],
            },
        ],
        {
            caller_addr: "%.6f" % (float(Decimal(utxo['amount']) - Decimal(fee)),),
            'contract': call_contract_script_hex,
        },
    )
    signed_call_contract_raw_tx_res = node.signrawtransaction(
        call_contract_raw_tx,
        [
            {
                'txid': utxo['txid'],
                'vout': utxo['vout'],
                'amount': utxo['amount'],
                'scriptPubKey': utxo['scriptPubKey'],
                'redeemScript': utxo.get('redeemScript', None),
            },
        ],
    )
    assert (signed_call_contract_raw_tx_res.get('complete', None) is True)
    signed_call_contract_raw_tx = signed_call_contract_raw_tx_res.get('hex')
    return node.sendrawtransaction(signed_call_contract_raw_tx)


def deposit_to_contract(node, caller_addr, contract_addr, deposit_amount, deposit_memo=" "):
    utxo = get_utxo(node, caller_addr)
    call_contract_script = CScript(
        [config['CONTRACT_VERSION'], deposit_memo.encode('utf8'), CScriptNum(int(deposit_amount * config['PRECISION'])),
         contract_addr.encode("utf8"),
         caller_addr.encode('utf8'),
         5000, 10, OP_DEPOSIT_TO_CONTRACT])
    call_contract_script_hex = call_contract_script.hex()
    call_contract_raw_tx = node.createrawtransaction(
        [
            {
                'txid': utxo['txid'],
                'vout': utxo['vout'],
            },
        ],
        {
            caller_addr: '%.6f' % float(Decimal(utxo['amount']) - Decimal(deposit_amount) - Decimal(0.01)),
            'contract': call_contract_script_hex,
        },
    )
    signed_call_contract_raw_tx_res = node.signrawtransaction(
        call_contract_raw_tx,
        [
            {
                'txid': utxo['txid'],
                'vout': utxo['vout'],
                'amount': utxo['amount'],
                'scriptPubKey': utxo['scriptPubKey'],
                'redeemScript': utxo.get('redeemScript', None),
            },
        ],
    )
    assert (signed_call_contract_raw_tx_res.get('complete', None) is True)
    signed_call_contract_raw_tx = signed_call_contract_raw_tx_res.get('hex')
    return node.sendrawtransaction(signed_call_contract_raw_tx)


def invoke_contract_api(node, caller_addr, contract_addr, api_name, api_arg, withdraw_infos=None, withdraw_froms=None, gas=5000, gas_price=10):
    utxo = get_utxo(node, caller_addr)
    call_contract_script = CScript(
        [config['CONTRACT_VERSION'], api_arg.encode('utf8'), api_name.encode('utf8'), contract_addr.encode("utf8"),
         caller_addr.encode('utf8'),
         gas, gas_price, OP_CALL])
    call_contract_script_hex = call_contract_script.hex()
    fee = 0.01
    vouts = {
        caller_addr: '%.6f' % float(Decimal(utxo['amount']) - Decimal(fee)),
        'contract': call_contract_script_hex,
    }

    if withdraw_infos is None:
        withdraw_infos = {}
    for k, v in withdraw_infos.items():
        if k == caller_addr:
            vouts[k] = "%.6f" % (float(vouts[k]) + float(v))
        else:
            vouts[k] = "%.6f" % float(v)
    if withdraw_froms is None:
        withdraw_froms = {}
    spend_contract_all_script_hexs = []
    for withdraw_from_contract_addr, withdraw_amount in withdraw_froms.items():
        spend_contract_script = CScript([
            int(Decimal(withdraw_amount) * config['PRECISION']), withdraw_from_contract_addr.encode('utf8'), OP_SPEND
        ])
        spend_contract_script_hex = spend_contract_script.hex()
        spend_contract_all_script_hexs.append(spend_contract_script_hex)
    if len(spend_contract_all_script_hexs) > 0:
        vouts['spend_contract'] = spend_contract_all_script_hexs
    call_contract_raw_tx = node.createrawtransaction(
        [
            {
                'txid': utxo['txid'],
                'vout': utxo['vout'],
            },
        ],
        vouts,
    )
    signed_call_contract_raw_tx_res = node.signrawtransaction(
        call_contract_raw_tx,
        [
            {
                'txid': utxo['txid'],
                'vout': utxo['vout'],
                'amount': utxo['amount'],
                'scriptPubKey': utxo['scriptPubKey'],
                'redeemScript': utxo.get('redeemScript', None),
            },
        ],
    )
    assert (signed_call_contract_raw_tx_res.get('complete', None) is True)
    signed_call_contract_raw_tx = signed_call_contract_raw_tx_res.get('hex')
    res = node.sendrawtransaction(signed_call_contract_raw_tx)
    return res


class SmartContractTest(BitcoinTestFramework):

    def assertTrue(self, cond, err="assertTrue error"):
        assert cond

    def assertEqual(self, a, b, err='assertEqual error'):
        assert a == b

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [[], []]
        self.account1 = 'a'
        self.account2 = 'b'
        self.account3 = 'c'
        self.address1 = None
        self.address2 = None
        self.address3 = None
        self.created_contract_addr = None

    def split_accounts(self):
        account1 = self.account1
        account2 = self.account2
        node1 = self.nodes[0]
        accounts = node1.listaccounts()
        if accounts[account1] > 5 * accounts[account2] and accounts[account1] > 21:
            node1.sendtoaddress(self.address2, 20)
        if accounts[account1] > 5 * accounts[account2] and accounts[account1] > 41:
            node1.sendtoaddress(self.address2, 20)
        if accounts[account1] > 5 * accounts[account2] and accounts[account1] > 61:
            node1.sendtoaddress(self.address2, 20)
        generate_block(node1, self.address1)

    def test_create_contract(self):
        print("test_create_contract")
        self.split_accounts()
        node1 = self.nodes[0]
        new_contract_addr = create_new_contract(node1, self.address1,
                                                os.path.dirname(__file__) + os.path.sep + "test.gpc")
        generate_block(node1, self.address1)
        print("new contract address: %s" % new_contract_addr)
        return new_contract_addr

    def test_create_native_contract(self):
        print("test_create_native_contract")
        self.split_accounts()
        node1 = self.nodes[0]
        new_contract_addr = create_new_native_contract(node1, self.address1, 'dgp')
        generate_block(node1, self.address1)
        print("new contract address: %s" % new_contract_addr)
        return new_contract_addr

    def test_demo_native_contract(self):
        print("test_create_native_contract")
        self.split_accounts()
        node1 = self.nodes[0]
        caller_addr = self.address1
        res = node1.registernativecontracttesting(caller_addr, 'demo')
        self.assertTrue(res['gasCount'] > 0)
        print("res: ", res)
        new_contract_addr = create_new_native_contract(node1, caller_addr, 'demo')
        generate_block(node1, caller_addr)
        print("new contract address: %s" % new_contract_addr)
        contract_addr = new_contract_addr
        contract_info = node1.getcontractinfo(contract_addr)
        print(contract_info)
        invoke_contract_api(node1, caller_addr, contract_addr, "hello", "abc 123")
        res = node1.invokecontractoffline(caller_addr, contract_addr, "hello", "abc")
        print("hello of demo result: ", res)
        self.assertEqual(res['result'], 'demo result')
        self.assertTrue(res['gasCount'] > 0)
        generate_block(node1, caller_addr)

    def test_dgp_native_contract(self):
        print("test_create_native_contract")
        self.split_accounts()
        caller_addr = self.address1
        node1 = self.nodes[0]
        res = node1.registernativecontracttesting(caller_addr, 'dgp')
        self.assertTrue(res['gasCount'] > 0)
        print("res: ", res)
        contract_addr = create_new_native_contract(node1, caller_addr, 'dgp')
        mine_res = generate_block(node1, caller_addr)
        print("mine_res: ", mine_res)
        print("new dgp contract address: %s" % contract_addr)
        admins = json.loads(node1.invokecontractoffline(caller_addr, contract_addr, 'admins', " ")['result'])
        print("admins after init is ", admins)
        self.assertEqual(admins, [caller_addr])
        min_gas_price = int(node1.invokecontractoffline(caller_addr, contract_addr, 'min_gas_price', " ")['result'])
        print("min_gas_price: ", min_gas_price)
        self.assertEqual(min_gas_price, 10)
        invoke_contract_api(node1, caller_addr, contract_addr, "create_change_admin_proposal",
                            json.dumps({"address": self.address2, "add": True, "needAgreeCount": 1}))
        # wait proposal votable for 10 blocks
        for i in range(10):
            generate_block(node1, caller_addr)
        invoke_contract_api(node1, caller_addr, contract_addr, "vote_admin", "true")
        generate_block(node1, self.address2)
        admins = json.loads(node1.invokecontractoffline(caller_addr, contract_addr, 'admins', " ")['result'])
        print("admins after add is ", admins)
        self.assertEqual(admins, [caller_addr, self.address2])

        invoke_contract_api(node1, caller_addr, contract_addr, "create_change_admin_proposal",
                            json.dumps({"address": self.address2, "add": False, "needAgreeCount": 2}))
        # wait proposal votable for 10 blocks
        for i in range(120):
            generate_block(node1, self.address2)
        invoke_contract_api(node1, caller_addr, contract_addr, "vote_admin", "true")
        invoke_contract_api(node1, self.address2, contract_addr, "vote_admin", "true")
        mine_res = generate_block(node1, self.address2)
        print("mine res: ", mine_res)
        admins = json.loads(node1.invokecontractoffline(caller_addr, contract_addr, 'admins', " ")['result'])
        print("admins after remove is ", admins)
        self.assertEqual(admins, [caller_addr])

        # test set_min_gas_price(change params)
        invoke_contract_api(node1, caller_addr, contract_addr, "set_min_gas_price",
                            "20,1")
        for i in range(10):
            generate_block(node1, caller_addr)
        invoke_contract_api(node1, caller_addr, contract_addr, "vote_change_param", "true")
        generate_block(node1, caller_addr)
        min_gas_price = int(
            node1.invokecontractoffline(caller_addr, contract_addr, 'min_gas_price', " ")['result'])
        print("min_gas_price: ", min_gas_price)
        self.assertEqual(min_gas_price, 20)

        # test change params and vote false
        invoke_contract_api(node1, caller_addr, contract_addr, "set_min_gas_price",
                            "30,1")
        for i in range(10):
            generate_block(node1, caller_addr)
        invoke_contract_api(node1, caller_addr, contract_addr, "vote_change_param", "false")
        generate_block(node1, caller_addr)
        min_gas_price = int(
            node1.invokecontractoffline(caller_addr, contract_addr, 'min_gas_price', " ")['result'])
        print("min_gas_price: ", min_gas_price)
        self.assertEqual(min_gas_price, 20)

    def test_get_contract_info(self):
        print("test_get_contract_info")
        self.split_accounts()
        node1 = self.nodes[0]
        contract_addr = self.created_contract_addr
        contract_info = node1.getcontractinfo(contract_addr)
        print(contract_info)
        self.assertEqual(contract_info['id'], contract_addr)
        if len(contract_info['name']) > 0:
            contract_info_found_by_name = node1.getcontractinfo(contract_info['name'])
            print("contract_info_found_by_name: ", contract_info_found_by_name)

    def test_get_simple_contract_info(self):
        print("test_get_simple_contract_info")
        node1 = self.nodes[0]
        contract_addr = self.created_contract_addr
        contract_info = node1.getsimplecontractinfo(contract_addr)
        print(contract_info)
        self.assertEqual(contract_info['id'], contract_addr)

    def test_get_current_root_state_hash(self):
        print("test_get_current_root_state_hash")
        self.split_accounts()
        node1 = self.nodes[0]
        root_state_hash = node1.currentrootstatehash()
        print("current root_state_hash: ", root_state_hash)
        height = node1.getblockcount()
        self.test_call_contract_api()
        mine_res = generate_block(node1, self.address1)
        new_root_state_hash = node1.currentrootstatehash()
        old_root_state_hash = node1.blockrootstatehash(height)
        print("new_root_state_hash: %s, old_root_state_hash: %s" % (new_root_state_hash, old_root_state_hash))
        self.assertEqual(old_root_state_hash, root_state_hash)
        is_current_rootstatehash_newer_info = node1.isrootstatehashnewer()
        print("is_current_rootstatehash_newer_info: ", is_current_rootstatehash_newer_info)
        self.assertTrue(not is_current_rootstatehash_newer_info['is_current_root_state_hash_after_best_block'])

    def test_call_contract_query_storage(self):
        print("test_call_contract_query_storage")
        self.split_accounts()
        node1 = self.nodes[0]
        contract_addr = self.created_contract_addr
        contract = node1.getcontractinfo(contract_addr)
        print("contract info: ", contract)
        invoke_res = node1.invokecontractoffline(
            self.address1, contract_addr, "query", "",
        )
        print("invoke result: ", invoke_res)
        self.assertEqual(invoke_res.get('result'), 'hello')
        print("gas used: ", invoke_res.get('gasCount'))
        self.assertTrue(invoke_res.get('gasCount') > 0)

    def test_call_contract_once_api(self):
        print("test_call_contract_once_api")
        self.split_accounts()
        node1 = self.nodes[0]
        contract_addr = create_new_contract(node1, self.address1, os.path.dirname(__file__) + os.path.sep + "test.gpc")
        print(contract_addr)
        generate_block(node1, self.address1)
        try:
            invoke_contract_api(node1, self.address1, contract_addr, "once", " ")
            generate_block(node1, self.address1)
            invoke_contract_api(node1, self.address1, contract_addr, "once", " ")
            mine_res = generate_block(node1, self.address1)
            print("mine res: ", mine_res)
            self.assertTrue(False)
        except Exception as e:
            print(e)
            self.assertTrue(str(e).find("only be called once") >= 0)
            pass

    def test_call_contract_offline(self):
        print("test_call_contract_offline")
        self.split_accounts()
        node1 = self.nodes[0]
        contract_addr = self.created_contract_addr
        contract = node1.getcontractinfo(contract_addr)
        print("contract info: ", contract)
        invoke_res = node1.invokecontractoffline(
            self.address1, contract_addr, "query", "abc",
        )
        print("invoke result: ", invoke_res)
        print("gas used: ", invoke_res.get('gasCount'))
        self.assertTrue(invoke_res.get('gasCount') > 0)
        self.assertEqual(invoke_res.get('result'), 'hello')

    def test_import_contract_by_address(self):
        print("test_import_contract_by_address")
        self.split_accounts()
        node1 = self.nodes[0]
        contract_addr = self.created_contract_addr
        invoke_res = node1.invokecontractoffline(
            self.address1, contract_addr, "import_contract_by_address_demo", "%s" % contract_addr,
        )
        print("invoke result: ", invoke_res)
        print("gas used: ", invoke_res.get('gasCount'))
        self.assertTrue(invoke_res.get('gasCount') > 0)
        self.assertEqual('hello world', invoke_res['result'])

    def test_register_contract_testing(self):
        print("test_register_contract_testing")
        self.split_accounts()
        node1 = self.nodes[0]
        bytecode_hex = read_contract_bytecode_hex(os.path.dirname(__file__) + os.path.sep + "test.gpc")
        invoke_res = node1.registercontracttesting(
            self.address1, bytecode_hex,
        )
        print("register contract testing result: ", invoke_res)
        print("gas used: ", invoke_res.get('gasCount'))
        self.assertTrue(invoke_res.get('gasCount') > 0)

    def test_upgrade_contract_testing(self):
        print("test_upgrade_contract_testing")
        self.split_accounts()
        node1 = self.nodes[0]
        contract_addr = self.test_create_contract()
        contract_name = "contract_name_%d" % random.randint(1, 99999999)
        invoke_res = node1.upgradecontracttesting(
            self.address1, contract_addr, contract_name, 'test contract desc',
        )
        print("upgrade contract testing result: ", invoke_res)
        print("gas used: ", invoke_res.get('gasCount'))
        self.assertTrue(invoke_res.get('gasCount') > 0)

    def test_deposit_to_contract_testing(self):
        print("test_deposit_to_contract_testing")
        self.split_accounts()
        node1 = self.nodes[0]
        invoke_res = node1.deposittocontracttesting(
            self.address1, self.created_contract_addr, 10, 'this is deposit memo',
        )
        print("deposit to contract testing result: ", invoke_res)
        print("gas used: ", invoke_res.get('gasCount'))
        self.assertTrue(invoke_res.get('gasCount') > 0)

    def test_get_transaction_contract_events(self):
        print("test_get_transaction_contract_events")
        self.split_accounts()
        node1 = self.nodes[0]
        # TODO: use real txid
        invoke_res = node1.gettransactionevents(
            '939421f700919cf1388c63c3e7dbfd79db432f9aa6e1e388bd31f42ed20025cb',
        )
        print("contract events result: ", invoke_res)

    def test_call_contract_api(self):
        print("test_call_contract_api")
        self.split_accounts()
        node1 = self.nodes[0]
        contract_addr = self.created_contract_addr
        contract = node1.getcontractinfo(contract_addr)
        print("contract info: ", contract)
        invoke_contract_api(node1, self.address1, contract_addr, "hello", "abc")
        mine_res = node1.generatetoaddress(
            1, self.address1, 1000000,
        )
        print("mine res: ", mine_res)

    def test_call_error_contract_api(self):
        print("test_call_error_contract_api")
        self.split_accounts()
        node1 = self.nodes[0]
        contract_addr = self.created_contract_addr
        contract = node1.getcontractinfo(contract_addr)
        print("contract info: ", contract)
        try:
            invoke_contract_api(node1, self.address1, contract_addr, "error",
                                " ")  # can't use empty string as api argument
            self.assertTrue(False)
        except Exception as e:
            print(e)
            print("error invoke contract passed successfully")

    def test_invalidate_contract_block(self):
        """generate a block with contract txs, then invalidate it."""
        print("test_invalidate_contract_block")
        node1 = self.nodes[0]
        contract_addr = create_new_contract(node1, self.address1, os.path.dirname(__file__) + os.path.sep + "test.gpc")
        generate_block(node1, self.address1, 1)

        old_root_state_hash = node1.currentrootstatehash()
        block_height1 = node1.getblockcount()
        balance0 = get_address_balance(node1, self.address1)
        deposit_amount = 10
        deposit_to_contract(node1, self.address1, contract_addr, deposit_amount, 'hi')
        balance1 = get_address_balance(node1, self.address1)
        self.assertTrue(balance0 > balance1 + deposit_amount) # some utxo is used for deposit and call contract
        block_hash = generate_block(node1, self.address2, 1)[0]
        print("block hash: %s" % block_hash)

        contract_info = node1.getsimplecontractinfo(contract_addr)
        print(contract_info)
        self.assertEqual(contract_info['id'], contract_addr)
        balance2 = get_address_balance(node1, self.address1)
        # self.assertTrue(balance1 > balance2)
        block_height2 = node1.getblockcount()
        self.assertEqual(block_height1 + 1, block_height2)
        root_state_hash_after_block = node1.currentrootstatehash()
        self.assertTrue(old_root_state_hash != root_state_hash_after_block)

        node1.invalidateblock(block_hash)
        block_height3 = node1.getblockcount()
        self.assertEqual(block_height1, block_height3)

        try:
            contract_info = node1.getsimplecontractinfo(contract_addr)
            print(contract_info)
            self.assertTrue(False, "shouldn't get contract info after rollbacked")
        except Exception as _:
            pass
        root_state_hash_after_invalidate = node1.currentrootstatehash()
        self.assertEqual(old_root_state_hash, root_state_hash_after_invalidate)

        balance3 = get_address_balance(node1, self.address1)
        print("balance0: %s, balance1: %s, balance2: %s, balance3: %s" % (str(balance0), str(balance1), str(balance2), str(balance3)))
        self.assertTrue(balance1 == balance3)  # txs in block invalated will back to mempool

        generate_block(node1, self.address1, 1)  # can't same user mine this block again in unittest

    def test_spend_utxo_withdrawed_from_contract(self):
        self.log.info("test_spend_utxo_withdrawed_from_contract")
        node1 = self.nodes[0]
        caller_addr = self.address1
        contract_addr = self.created_contract_addr
        contract = node1.getcontractinfo(contract_addr)
        print("contract info: ", contract)
        deposit_amount1 = 0.3
        deposit_to_contract(node1, caller_addr, contract_addr, deposit_amount1, "memo123")
        generate_block(node1, caller_addr, 1)

        # withdraw from contract
        withdraw_amount = "0.3"
        withdraw_txid = invoke_contract_api(node1, caller_addr, contract_addr, "withdraw",
                                          str(int(Decimal(withdraw_amount) * config['PRECISION'])), {
                                              caller_addr: Decimal(withdraw_amount)
                                          }, {
                                              contract_addr: Decimal(withdraw_amount)
                                          })
        withdraw_received_amount = Decimal(withdraw_amount) - Decimal(0.01)
        generate_block(node1, self.address3, 1)
        withdraw_tx = node1.decoderawtransaction(node1.getrawtransaction(withdraw_txid))
        print("withdraw tx: ", withdraw_tx)
        withdraw_tx_out = list(filter(lambda o : (o['scriptPubKey']['type'] == 'pubkeyhash' or o['scriptPubKey']['type']=='scripthash') and len(o['scriptPubKey'].get('addresses', []))>0 and o['scriptPubKey']['addresses'][0] == caller_addr, withdraw_tx['vout']))[0]['n']
        withdraw_tx_outpoint = withdraw_tx['vout'][withdraw_tx_out]
        print("withdraw out: ", withdraw_tx_outpoint)
        generate_block(node1, self.address3, 100)  # make sure no new matured block-rewards

        old_balance_of_address1 = get_address_balance(node1, self.address1)
        old_balance_of_address2 = get_address_balance(node1, self.address2)
        # spend the tx-out
        fee = 0.0001
        vin = [
            {'txid': withdraw_txid, 'vout': withdraw_tx_out},
        ]
        vout = {
            self.address2: '%.6f' % float(Decimal(withdraw_received_amount) - Decimal(fee)),
        }
        # vout too large, need change
        if withdraw_tx_outpoint['value'] > Decimal(withdraw_received_amount):
            vout[self.address1] = "%.6f" % float(Decimal(withdraw_tx_outpoint['value']) - Decimal(withdraw_received_amount))

        print("vin", vin)
        print("vout", vout)
        spend_tx_hex = node1.createrawtransaction(vin, vout)
        address1_info = node1.validateaddress(self.address1)
        address1_pubkey = address1_info['scriptPubKey']
        print(spend_tx_hex)
        print('address1_info: ', address1_info)
        signed_spend_tx_hex = node1.signrawtransaction(spend_tx_hex, [
            {
                'txid': withdraw_txid,
                'vout': withdraw_tx_out, 
                'scriptPubKey': address1_pubkey,
                'amount': withdraw_tx_outpoint['value'],
                'scriptPubKey': address1_pubkey,
                    'redeemScript': address1_info.get('redeemScript', None),
            }
        ])['hex']
        spend_txid = node1.sendrawtransaction(signed_spend_tx_hex)
        generate_block(node1, self.address3, 1)
        new_balance_of_address1 = get_address_balance(node1, self.address1)
        new_balance_of_address2 = get_address_balance(node1, self.address2)
        self.log.info("withdraw_received_amount: %.6f" % float(withdraw_received_amount))
        print(old_balance_of_address1, old_balance_of_address2, new_balance_of_address1, new_balance_of_address2)
        self.assertEqual("%.6f" % float(new_balance_of_address2), "%.6f" % float(Decimal(old_balance_of_address2) + Decimal(withdraw_received_amount) - Decimal(fee)))
        self.assertEqual("%.6f" % float(new_balance_of_address1), "%.6f" % float(Decimal(old_balance_of_address1) - Decimal(withdraw_received_amount)))

    def deposit_to_contract(self, mine=True):
        print("deposit_to_contract")
        if mine:
            self.split_accounts()
        node1 = self.nodes[0]
        caller_addr = self.address1
        contract_addr = self.created_contract_addr
        contract = node1.getcontractinfo(contract_addr)
        # print("contract info: ", contract)
        deposit_amount1 = 0.1
        deposit_to_contract(node1, caller_addr, contract_addr, deposit_amount1, "memo123")
        # put another tx again
        deposit_to_contract(node1, caller_addr, contract_addr, 0.2, "memo1234")
        if mine:
            mine_res = node1.generatetoaddress(
                1, self.address1, 1000000,
            )
            print("mine res: ", mine_res)
            contract_info = node1.getcontractinfo(contract_addr)
            print("contract_info after deposit is: ", contract_info)
            invoke_res = node1.invokecontractoffline(
                self.address1, contract_addr, "query_money", "",
            )
            print("storage.money after deposit: ", invoke_res)
            money_storage = node1.getcontractstorage(contract_addr, 'money')['value']
            print("money_storage: ", money_storage)
            self.assertEqual(str(money_storage), str(invoke_res['result']))
            self.assertTrue(len(contract_info['balances']) > 0)
            self.assertEqual(int(invoke_res['result']), contract_info['balances'][0]['amount'])

    def multi_contract_balance_change(self):
        print("multi_contract_balance_change")
        # TODO

    def withdraw_from_contract(self, mine=True, withdraw_to_addr=None):
        if withdraw_to_addr is None:
            withdraw_to_addr = self.address3
        print("withdraw_from_contract")
        if mine:
            self.split_accounts()
        node1 = self.nodes[0]
        contract_addr = self.created_contract_addr
        contract = node1.getcontractinfo(contract_addr)
        # print("contract info: ", contract)
        account_balance_before_withdraw = get_address_balance(node1, self.address1)
        account_balance_before_withdraw_of_address3 = get_address_balance(node1, self.address3)
        withdraw_amount = "0.3"
        arg = str(int(Decimal(withdraw_amount) * config['PRECISION']))
        res = node1.invokecontractoffline(self.address1, contract_addr, "withdraw", arg)
        print("invoke testing res: ", res)
        res = invoke_contract_api(node1, self.address1, contract_addr, "withdraw",
                                  arg, {
                                      withdraw_to_addr: Decimal(withdraw_amount)
                                  }, {
                                      contract_addr: Decimal(withdraw_amount)
                                  })
        fee = 0.01
        print("txid: ", res)
        if mine:
            mine_res = node1.generatetoaddress(
                1, self.address2, 1000000,
            )
            print("mine res: ", mine_res)
            contract_info = node1.getcontractinfo(contract_addr)
            print("contract_info after withdraw is: ", contract_info)
            invoke_res = node1.invokecontractoffline(
                self.address1, contract_addr, "query_money", "",
            )['result']
            print("storage.money after withdraw: ", invoke_res)
            account_balance_after_withdraw = get_address_balance(node1, self.address1)
            print("account change of withdraw-from-contract: %f to %f" % (
                account_balance_before_withdraw, account_balance_after_withdraw))
            mine_reward = 0  # address2 mine the block
            self.assertEqual(
                "%.6f" % float(Decimal(account_balance_before_withdraw) + Decimal(mine_reward) - Decimal(fee)),
                "%.6f" % float(account_balance_after_withdraw))
            if withdraw_to_addr == self.address3:
                account_balance_after_withdraw_of_address3 = get_address_balance(node1, self.address3)
                print("account3 balance change from %f to %f" % (
                account_balance_before_withdraw_of_address3, account_balance_after_withdraw_of_address3))
                self.assertEqual(
                    "%.6f" % float(Decimal(account_balance_before_withdraw_of_address3) + Decimal(withdraw_amount)),
                    "%.6f" % float(account_balance_after_withdraw_of_address3))

    def test_upgrade_contract(self):
        print("test_upgrade_contract")
        self.split_accounts()
        node1 = self.nodes[0]
        contract_addr = self.test_create_contract()
        contract = node1.getcontractinfo(contract_addr)
        print("contract info: ", contract)
        contract_name = "contract_name_%d" % random.randint(1, 99999999)
        upgrade_contract(node1, self.address1, contract_addr, contract_name, "this is contract desc")
        generate_block(node1, self.address1)

    def test_many_contract_invokes_in_one_block(self):
        print("test_many_contract_invokes_in_one_block")
        self.split_accounts()
        node1 = self.nodes[0]
        caller_addr = self.address1
        contract_addr = self.created_contract_addr
        contract = node1.getsimplecontractinfo(contract_addr)
        n1 = 10
        n2 = 10
        # if contract balance not enough to withdraw, need to deposit some to it before test
        if len(contract['balances']) < 1 or contract['balances'][0]['amount'] < (n2 * 0.3):
            deposit_to_contract(node1, caller_addr, contract_addr, n2 * 0.3, "memo1234")
            generate_block(node1, caller_addr)

        # make sure coins mature
        mine_res = generate_block(node1, self.address2, 100)

        invoke_res = node1.invokecontractoffline(
            caller_addr, contract_addr, "query_money", "",
        )['result']
        print("storage.money before many deposits and withdraws: ", invoke_res)

        account_balance_before_withdraw = get_address_balance(node1, caller_addr)
        fee = 0.01
        # invoke_contract_api(caller_addr, contract_addr, "hello", "abc", None, None)
        for i in range(n1):
            self.deposit_to_contract(False)
        for i in range(n2):
            self.withdraw_from_contract(False, self.address1)
        mine_res = generate_block(node1, self.address2)
        mine_block_id = mine_res[0]
        block = node1.getblock(mine_block_id)
        mine_reward = ub_utils.calc_block_reward('regtest', block['height']) * 1.0 / config['PRECISION']
        print("mine res: ", mine_res)
        print('mine reward: %s' % str(mine_reward))
        contract_info = node1.getcontractinfo(contract_addr)
        # print("contract_info after withdraw is: ", contract_info)
        invoke_res = node1.invokecontractoffline(
            caller_addr, contract_addr, "query_money", "",
        )['result']
        print("storage.money after many deposits and withdraws: ", invoke_res)
        mine_res = generate_block(node1, self.address2, 100)
        assert len(mine_res) == 100
        account_balance_after_withdraw = get_address_balance(node1, caller_addr)
        print("account change of withdraw-from-contract: %f to %f" % (
            account_balance_before_withdraw, account_balance_after_withdraw))
        self.assertEqual(
            "%.6f" % (account_balance_before_withdraw + Decimal(- fee * (2*n1+n2) - 0.3 * n1 + 0.3 * n2),),
            "%.6f" % account_balance_after_withdraw)

    def test_gas_not_enough(self):
        print("test_gas_not_enough")
        self.split_accounts()
        node1 = self.nodes[0]
        contract_addr = self.created_contract_addr
        contract = node1.getcontractinfo(contract_addr)
        print("contract info: ", contract)
        try:
            invoke_contract_api(node1, self.address1, contract_addr, "large", "abc")
            self.assertTrue(False)
        except Exception as e:
            print(e)

    def test_global_apis(self):
        print("test_global_apis")
        self.split_accounts()
        node1 = self.nodes[0]
        contract_addr = self.created_contract_addr
        contract = node1.getcontractinfo(contract_addr)
        print("contract info: ", contract)
        utxo = get_utxo(node1, self.address1)
        call_contract_script = CScript(
            [config['CONTRACT_VERSION'], "abc".encode('utf8'), "test_apis".encode('utf8'), contract_addr.encode("utf8"),
             self.address1.encode('utf8'),
             5000, 10, OP_CALL])
        call_contract_script_hex = call_contract_script.hex()
        call_contract_raw_tx = node1.createrawtransaction(
            [
                {
                    'txid': utxo['txid'],
                    'vout': utxo['vout'],
                },
            ],
            {
                self.address1: '%.6f' % float(Decimal(utxo['amount']) - Decimal(0.01)),
                'contract': call_contract_script_hex,
            },
        )
        signed_call_contract_raw_tx_res = node1.signrawtransaction(
            call_contract_raw_tx,
            [
                {
                    'txid': utxo['txid'],
                    'vout': utxo['vout'],
                    'amount': utxo['amount'],
                    'scriptPubKey': utxo['scriptPubKey'],
                    'redeemScript': utxo.get('redeemScript', None),
                },
            ],
        )
        self.assertEqual(signed_call_contract_raw_tx_res.get('complete', None), True)
        signed_call_contract_raw_tx = signed_call_contract_raw_tx_res.get('hex')
        print(signed_call_contract_raw_tx)
        node1.sendrawtransaction(signed_call_contract_raw_tx)
        mine_res = node1.generatetoaddress(
            1, self.address1, 1000000,
        )
        print("mine res: ", mine_res)

    def test_token_contract(self):
        print("test_token_contract")
        self.split_accounts()
        node1 = self.nodes[0]
        admin_addr = self.address1
        other_addr = self.address2

        for i in range(20):
            generate_block(node1, other_addr)
        for i in range(20):
            generate_block(node1, admin_addr)

        # create token contract
        utxo = get_utxo(node1, admin_addr)
        bytecode_hex = read_contract_bytecode_hex(os.path.dirname(__file__) + os.path.sep + "newtoken.gpc")
        res = node1.registercontracttesting(admin_addr, bytecode_hex)
        print("token contract testing register response: ", res)
        register_contract_script = CScript(
            [config['CONTRACT_VERSION'], bytes().fromhex(bytecode_hex), admin_addr.encode('utf8'),
             10000, 10, OP_CREATE])
        create_contract_script = register_contract_script.hex()
        # print("create_contract_script size %d" % len(create_contract_script))
        create_contract_raw_tx = node1.createrawtransaction(
            [
                {
                    'txid': utxo['txid'],
                    'vout': utxo['vout'],
                },
            ],
            {
                admin_addr: '%.6f' % float(Decimal(utxo['amount']) - Decimal(0.01)),
                'contract': create_contract_script,
            },
        )
        signed_create_contract_raw_tx_res = node1.signrawtransaction(
            create_contract_raw_tx,
            [
                {
                    'txid': utxo['txid'],
                    'vout': utxo['vout'],
                    'amount': utxo['amount'],
                    'scriptPubKey': utxo['scriptPubKey'],
                    'redeemScript': utxo.get('redeemScript', None),
                },
            ],
        )
        self.assertEqual(signed_create_contract_raw_tx_res.get('complete', None), True)
        signed_create_contract_raw_tx = signed_create_contract_raw_tx_res.get('hex')
        # print(signed_create_contract_raw_tx)
        node1.sendrawtransaction(signed_create_contract_raw_tx)
        generate_block(node1, admin_addr)
        contract_addr = node1.getcreatecontractaddress(
            signed_create_contract_raw_tx
        )['address']
        print("new contract address: %s" % contract_addr)
        contract = node1.getcontractinfo(contract_addr)
        print("contract info: ", contract)
        print("create contract of token tests passed")

        # init config of token contract
        res = node1.invokecontractoffline(admin_addr, contract_addr, "init_token", "test,TEST,1000000,100")
        print(res)
        invoke_contract_api(node1, admin_addr, contract_addr, "init_token", "test,TEST,1000000,100", gas=25000)
        generate_block(node1, admin_addr)
        state = node1.invokecontractoffline(
            admin_addr, contract_addr, "state", " ",
        )['result']
        self.assertEqual(state, "COMMON")
        token_balance = node1.invokecontractoffline(
            admin_addr, contract_addr, "balanceOf", "%s" % admin_addr,
        )['result']
        self.assertEqual(int(token_balance), 1000000)
        print("init config of token tests passed")

        # transfer
        res = node1.invokecontractoffline(admin_addr, contract_addr, "transfer", "%s,%d" % (other_addr, 10000))
        print(res)
        invoke_contract_api(node1, admin_addr, contract_addr, "transfer", "%s,%d" % (other_addr, 10000), gas=65000)
        generate_block(node1, admin_addr)
        token_balance = node1.invokecontractoffline(
            admin_addr, contract_addr, "balanceOf", "%s" % admin_addr,
        )['result']
        self.assertEqual(int(token_balance), 1000000 - 10000)
        other_token_balance = node1.invokecontractoffline(
            admin_addr, contract_addr, "balanceOf", "%s" % other_addr,
        )['result']
        self.assertEqual(int(other_token_balance), 10000)
        print("transfer of token tests passed")

        # approve balance
        invoke_contract_api(node1, admin_addr, contract_addr, "approve", "%s,%d" % (other_addr, 20000))
        generate_block(node1, admin_addr)
        token_balance = node1.invokecontractoffline(
            admin_addr, contract_addr, "balanceOf", "%s" % admin_addr,
        )['result']
        self.assertEqual(int(token_balance), 1000000 - 10000)
        other_token_balance = node1.invokecontractoffline(
            admin_addr, contract_addr, "balanceOf", "%s" % other_addr,
        )['result']
        self.assertEqual(int(other_token_balance), 10000)
        all_approved_token_from_admin = node1.invokecontractoffline(
            admin_addr, contract_addr, "allApprovedFromUser", "%s" % admin_addr,
        )['result']
        print("all_approved_token_from_admin: ", all_approved_token_from_admin)
        other_approved_token_balance = node1.invokecontractoffline(
            admin_addr, contract_addr, "approvedBalanceFrom", "%s,%s" % (other_addr, admin_addr),
        )['result']
        self.assertEqual(int(other_approved_token_balance), 20000)
        print("approve of token tests passed")

        # transferFrom
        invoke_contract_api(node1, other_addr, contract_addr, "transferFrom",
                            "%s,%s,%d" % (admin_addr, other_addr, 500), gas=10000)
        generate_block(node1, admin_addr)
        token_balance = node1.invokecontractoffline(
            admin_addr, contract_addr, "balanceOf", "%s" % admin_addr,
        )['result']
        self.assertEqual(int(token_balance), 1000000 - 10000 - 500)
        other_token_balance = node1.invokecontractoffline(
            admin_addr, contract_addr, "balanceOf", "%s" % other_addr,
        )['result']
        self.assertEqual(int(other_token_balance), 10000 + 500)
        other_approved_token_balance = node1.invokecontractoffline(
            admin_addr, contract_addr, "approvedBalanceFrom", "%s,%s" % (other_addr, admin_addr),
        )['result']
        print("other_approved_token_balance after transferFrom is ", other_approved_token_balance)
        self.assertEqual(int(other_approved_token_balance), 20000 - 500)
        print("transfer of token tests passed")

        # lock balance
        invoke_contract_api(node1, admin_addr, contract_addr, "openAllowLock", " ")
        generate_block(node1, admin_addr)
        cur_blockcount = node1.getblockcount()
        locked_amount = 300
        unlock_blocknum = cur_blockcount + 2
        invoke_contract_api(node1, admin_addr, contract_addr, "lock", "%d,%d" % (locked_amount, unlock_blocknum))
        generate_block(node1, admin_addr)
        token_balance = node1.invokecontractoffline(
            admin_addr, contract_addr, "balanceOf", "%s" % admin_addr,
        )['result']
        self.assertEqual(int(token_balance), 1000000 - 10000 - 500 - locked_amount)
        locked_balance = node1.invokecontractoffline(admin_addr, contract_addr, "lockedBalanceOf", "%s" % admin_addr)[
            'result']
        self.assertEqual(locked_balance, "%s,%d" % (locked_amount, unlock_blocknum))
        generate_block(node1, admin_addr)
        invoke_contract_api(node1, admin_addr, contract_addr, "unlock", " ")
        generate_block(node1, admin_addr)
        token_balance = node1.invokecontractoffline(
            admin_addr, contract_addr, "balanceOf", "%s" % admin_addr,
        )['result']
        self.assertEqual(int(token_balance), 1000000 - 10000 - 500)
        locked_balance = node1.invokecontractoffline(admin_addr, contract_addr, "lockedBalanceOf", "%s" % admin_addr)[
            'result']
        self.assertEqual(locked_balance, "0,0")
        print("lock of token tests passed")

    def test_price_feeder_contract(self):
        print("test_price_feeder_contract")
        self.split_accounts()
        node1 = self.nodes[0]
        caller_addr = self.address1
        other_addr = self.address2
        try:
            contract = node1.getsimplecontractinfo("price_feeder")
        except Exception as e:
            print(e)
            contract_addr = create_new_contract(node1, caller_addr,
                                                os.path.dirname(__file__) + os.path.sep + "price_feeder.gpc")
            generate_block(node1, caller_addr)
            upgrade_contract(node1, caller_addr, contract_addr, "price_feeder", "price feeder contract desc")
            generate_block(node1, caller_addr)
            contract = node1.getsimplecontractinfo("price_feeder")
        contract_addr = contract['id']
        print(contract_addr)
        print(contract)
        owner_addr = node1.invokecontractoffline(
            caller_addr, contract_addr, "owner", " ",
        )['result']
        self.assertEqual(owner_addr, caller_addr)
        to_feed_tokens = json.loads(node1.invokecontractoffline(
            caller_addr, contract_addr, "to_feed_tokens", " ",
        )['result'])
        if len(to_feed_tokens) < 1:
            res = invoke_contract_api(node1, caller_addr, contract_addr, "add_feed_token", "%s,%d" % ("CNY", 10 ** 8))
            generate_block(node1, caller_addr)
            to_feed_tokens = json.loads(node1.invokecontractoffline(
                caller_addr, contract_addr, "to_feed_tokens", " ",
            )['result'])
        print("to feed tokens: ", to_feed_tokens)
        feeders = json.loads(node1.invokecontractoffline(
            caller_addr, contract_addr, "feeders", " ",
        )['result'])
        print("feeders: ", feeders)
        if len(feeders) == 1 and feeders[0] == caller_addr:
            invoke_contract_api(node1, caller_addr, contract_addr, "add_feeder", "%s" % other_addr)
            generate_block(node1, caller_addr)
            feeders_after_add = json.loads(node1.invokecontractoffline(
                caller_addr, contract_addr, "feeders", " ",
            )['result'])
            print("feeders_after_add: ", feeders_after_add)
            self.assertEqual(len(feeders_after_add), 2)
            invoke_contract_api(node1, caller_addr, contract_addr, "remove_feeder", "%s" % other_addr)
            generate_block(node1, caller_addr)
            feeders_after_removed = json.loads(node1.invokecontractoffline(
                caller_addr, contract_addr, "feeders", " ",
            )['result'])
            print("feeders_after_removed: ", feeders_after_removed)
            self.assertEqual(len(feeders_after_removed), 1)
        elif len(feeders) > 1:
            invoke_contract_api(node1, caller_addr, contract_addr, "remove_feeder", "%s" % other_addr)
            generate_block(node1, caller_addr)
            feeders_after_removed = json.loads(node1.invokecontractoffline(
                caller_addr, contract_addr, "feeders", " ",
            )['result'])
            print("feeders_after_removed: ", feeders_after_removed)
        all_token_prices = json.loads(node1.invokecontractoffline(
            caller_addr, contract_addr, "all_token_prices", " ",
        )['result'])
        print("all_token_prices: ", all_token_prices)
        price_of_cny = json.loads(node1.invokecontractoffline(
            caller_addr, contract_addr, "price_of_token", "CNY",
        )['result'])
        print("price of cny: ", price_of_cny)

    def test_create_contract_rpc(self):
        print("test_create_contract_rpc")
        self.split_accounts()
        node1 = self.nodes[0]
        caller_addr = self.address1
        bytecode_hex = read_contract_bytecode_hex(os.path.dirname(__file__) + os.path.sep + "test.gpc")
        signed_create_contract_tx = node1.createcontract(caller_addr, bytecode_hex, 5000, 10, "0.001")
        print("signed_create_contract_tx: ", signed_create_contract_tx)
        decoded_create_contract_tx = node1.decoderawtransaction(signed_create_contract_tx)
        print("decoded_create_contract_tx: ", decoded_create_contract_tx)
        contract_addr = node1.getcreatecontractaddress(signed_create_contract_tx)['address']
        print("contract addr: ", contract_addr)
        node1.sendrawtransaction(signed_create_contract_tx)
        generate_block(node1, caller_addr)
        print("contract: ", node1.getsimplecontractinfo(contract_addr))

        deposit_to_contract(node1, caller_addr, contract_addr, 1)
        generate_block(node1, caller_addr)
        contract = node1.getsimplecontractinfo(contract_addr)
        print(contract)
        assert len(contract['balances'])>0 and contract['balances'][0]['amount'] == 1*config['PRECISION']

        signed_call_contract_tx = node1.callcontract(caller_addr, contract_addr, "withdraw", "%d" % int(0.5 * config['PRECISION']), 5000, 10, "0.005")
        node1.sendrawtransaction(signed_call_contract_tx)
        generate_block(node1, caller_addr)
        contract = node1.getsimplecontractinfo(contract_addr)
        assert(contract['balances'][0]['amount'] == int(0.5*config['PRECISION']))
        print("called contract withdraw api successfully")

    def test_constant_value_token_contract(self):
        print("test_constant_value_token_contract")
        self.split_accounts()
        node1 = self.nodes[0]
        caller_addr = self.address1
        other_addr = self.address2
        self.test_price_feeder_contract()
        contract_addr = create_new_contract(node1, caller_addr,
                                            os.path.dirname(__file__) + os.path.sep + "new_any_mortgage_token.gpc")
        generate_block(node1, caller_addr)
        state = node1.invokecontractoffline(
            caller_addr, contract_addr, "state", " ",
        )['result']
        self.assertEqual(state, "NOT_INITED")
        invoke_contract_api(node1, caller_addr, contract_addr,
                            "init_token", "%s,%d,%d,%s,%d" % ("test", 1000000, 100, "CNY", 110000), gas=10000)
        generate_block(node1, caller_addr)
        state = node1.invokecontractoffline(
            caller_addr, contract_addr, "state", " ",
        )['result']
        self.assertEqual(state, "COMMON")
        deposit_to_contract(node1, caller_addr, contract_addr, 0.1, "memo123")
        invoke_contract_api(node1, caller_addr, contract_addr, "mint", "200000")
        generate_block(node1, caller_addr)
        admin = node1.invokecontractoffline(
            caller_addr, contract_addr, "admin", " ",
        )['result']
        print("admin: ", admin)
        self.assertEqual(admin, caller_addr)
        total_supply = int(node1.invokecontractoffline(
            caller_addr, contract_addr, "totalSupply", " ",
        )['result'])
        self.assertEqual(total_supply, 1200000)
        precision = int(node1.invokecontractoffline(
            caller_addr, contract_addr, "precision", " ",
        )['result'])
        print("precision: ", precision)
        self.assertEqual(precision, 100)
        token_name = node1.invokecontractoffline(
            caller_addr, contract_addr, "tokenName", " ",
        )['result']
        print("token name: ", token_name)
        self.assertEqual(token_name, "test")
        invoke_contract_api(node1, caller_addr, contract_addr, "destroy", "100000")
        generate_block(node1, caller_addr)
        total_supply = int(node1.invokecontractoffline(
            caller_addr, contract_addr, "totalSupply", " ",
        )['result'])
        self.assertEqual(total_supply, 1100000)
        mortgage_rate = node1.invokecontractoffline(
            caller_addr, contract_addr, "mortgageRate", " ",
        )['result']
        print("mortgage_rate: ", mortgage_rate)
        self.assertEqual(mortgage_rate, "0.000008")
        mortgage_balance = int(node1.invokecontractoffline(
            caller_addr, contract_addr, "mortgageBalance", " ",
        )['result'])
        print("mortgage_balance before withdraw: ", mortgage_balance)
        self.assertEqual(mortgage_balance, 0.1 * config['PRECISION'])

        withdraw_res = node1.invokecontractoffline(
            caller_addr, contract_addr, "withdraw_unused", "%d" % (0.05 * config['PRECISION']),
        )
        print(withdraw_res)
        withdraw_from_infos = {}
        for change in withdraw_res['balanceChanges']:
            if change["is_contract"] and not change["is_add"]:
                withdraw_from_infos[change["address"]] = change["amount"] * 1.0 / config["PRECISION"]
        withdraw_infos = {}
        for change in withdraw_res["balanceChanges"]:
            if not change["is_contract"] and change["is_add"]:
                withdraw_infos[change["address"]] = change["amount"] * 1.0 / config["PRECISION"]
        invoke_contract_api(node1, caller_addr, contract_addr, "withdraw_unused", "%d" % (0.05 * config['PRECISION']),
                            withdraw_infos, withdraw_from_infos)
        generate_block(node1, caller_addr)
        mortgage_rate = node1.invokecontractoffline(
            caller_addr, contract_addr, "mortgageRate", " ",
        )['result']
        print("mortgage_rate after withdraw: ", mortgage_rate)
        self.assertEqual(mortgage_rate, "0.000004")
        mortgage_balance = int(node1.invokecontractoffline(
            caller_addr, contract_addr, "mortgageBalance", " ",
        )['result'])
        print("mortgage_balance after withdraw: ", mortgage_balance)
        self.assertEqual(mortgage_balance, 0.05 * config['PRECISION'])

    def setup_network(self, split=False):
        super().setup_network()
        connect_nodes_bi(self.nodes, 0, 1)

        node1 = self.nodes[0]
        address_mode = '' # 'legacy'
        self.address1 = node1.getnewaddress(self.account1, address_mode)
        self.address2 = node1.getnewaddress(self.account2, "bech32")
        self.address3 = node1.getnewaddress(self.account3, address_mode)

        print("address1: %s\naddress2: %s\naddress3: %s" % (self.address1, self.address2, self.address3))

        generate_block(node1, self.address1, 700)
        generate_block(node1, self.address2, 700)
        generate_block(node1, self.address3, 99)
        try:
            self.test_create_contract()
            self.assertTrue(False, "contract is disabled before height 1500")
        except Exception as e:
            pass
        generate_block(node1, self.address3, 1)
        
        tx_id = node1.sendtoaddress(self.address2, 1)
        raw_tx = node1.getrawtransaction(tx_id)
        tx = node1.decoderawtransaction(raw_tx)
        print("simple tx: ", tx)

    def test_big_data(self):
        print("test_big_data")
        self.split_accounts()
        caller_addr = self.address1
        node1 = self.nodes[0]
        contract_addr = create_new_contract(node1, caller_addr, os.path.dirname(__file__) + os.path.sep + 'test_big_data.gpc')
        generate_block(node1, caller_addr)
        print("test big data contract: ", contract_addr)
        try:
            invoke_res = node1.invokecontractoffline(
                config['MAIN_USER_ADDRESS'], contract_addr, "fastwrite_bigdata", "1111111111111111111"
            )
            self.assertTrue(False, 'need out of gas but not')
        except Exception as e:
            print(e)

    def run_test(self):
        self.created_contract_addr = self.test_create_contract()
        native_contract_addr = self.test_create_native_contract()

        self.test_dgp_native_contract()
        self.test_get_contract_info()
        self.test_get_simple_contract_info()
        self.test_get_current_root_state_hash()
        self.test_call_contract_query_storage()
        self.test_call_contract_once_api()
        self.test_call_contract_offline()
        self.test_import_contract_by_address()
        self.test_register_contract_testing()
        self.test_upgrade_contract_testing()
        self.test_deposit_to_contract_testing()
        self.test_get_transaction_contract_events()
        self.test_call_contract_api()
        self.test_call_error_contract_api()
        self.deposit_to_contract()
        self.multi_contract_balance_change()
        self.withdraw_from_contract()
        self.test_upgrade_contract()
        self.test_many_contract_invokes_in_one_block()
        self.test_gas_not_enough()
        self.test_global_apis()
        self.test_token_contract()
        self.test_price_feeder_contract()
        self.test_constant_value_token_contract()
        self.test_invalidate_contract_block()
        self.test_spend_utxo_withdrawed_from_contract()
        self.test_create_contract_rpc()
        self.test_big_data()

        generate_block(self.nodes[0], self.address1, 1)
        self.sync_all()
        print("block-height after all tests is: %d" % self.nodes[0].getblockcount())
        self.assertEqual(self.nodes[0].getblockcount(), self.nodes[1].getblockcount())


if __name__ == '__main__':
    SmartContractTest().main()
