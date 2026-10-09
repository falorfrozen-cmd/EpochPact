"""Snapshot consistency and collection semantics; no access to the real game."""
import copy
import unittest
from unittest.mock import patch
from . import collection_backend as c, monolith_backend as m


def item(uid=1, lp=0, kind="Unique", key="a"):
    return dict(uniqueId=uid, lp=lp, kind=kind, quantity=1, name="Test", type=0, subType=0,
                baseType="Helmet", key=key, weaversWill=0)


def page(items, offset=0, total=None, nxt=None, revision="stable", owner="6"):
    return dict(ok=True, player=dict(id=owner, name="Test"), scope="stash", revision=revision,
                items=items, offset=offset, next=nxt, total=len(items) if total is None else total, tabs=1, readMs=1)


class CollectionTests(unittest.TestCase):
    def test_sequential_pages_are_complete(self):
        replies=[page([item(key=str(i)) for i in range(100)],total=101,nxt=100),page([item(key="100")],100,101)]
        with patch.object(c.ipc,"request",side_effect=replies) as send:
            result=c.read_items()
        self.assertEqual(len(result['items']),101)
        self.assertEqual(result['pages'],2)
        self.assertEqual([a.args[0] for a in send.call_args_list],['stashread 0','stashread 100'])

    def test_snapshot_changes_reject_partial_results(self):
        for changed in (dict(revision="new"),dict(owner="7"),dict(total=102)):
            second=page([item()],100,101,**{k:v for k,v in changed.items() if k!='total'})
            if 'total' in changed: second['total']=changed['total']
            with patch.object(c.ipc,"request",side_effect=[page([item()]*100,total=101,nxt=100),second]),self.assertRaises(RuntimeError):
                c.read_items()

    def test_bad_continuations_reject_no_loop(self):
        for nxt in (0,-1,True,101,1000):
            with patch.object(c.ipc,"request",return_value=page([item()],total=101,nxt=nxt)),self.assertRaises(RuntimeError):
                c.read_items()

    def test_short_snapshot_rejected(self):
        with patch.object(c.ipc,"request",return_value=page([item()],total=3)),self.assertRaises(RuntimeError): c.read_items()

    def test_native_failure_is_not_empty_success(self):
        with patch.object(c.ipc,"request",return_value=dict(ok=False,error="transition")),self.assertRaisesRegex(RuntimeError,"transition"): c.read_items()

    def test_empty_stash_is_valid(self):
        with patch.object(c.ipc,"request",return_value=page([])): self.assertEqual(c.stash()['groups'],[])

    def test_legendary_and_weaver_do_not_claim_craftable_lp(self):
        copies=[item(lp=0),item(lp=2,key='b'),item(lp=4,kind='Legendary',key='c'),dict(item(lp=4,key='d'),weaversWill=20)]
        catalog=dict(ok=True,player=dict(id='6'),items=[dict(id=1,name='Test'),dict(id=2,name='Missing')])
        with patch.object(c.ipc,"request",return_value=catalog),patch.object(c,"read_items",return_value=dict(page(copies),pages=1,maxPageMs=1)):
            result=c.atlas()
        self.assertEqual((result['items'][0]['count'],result['items'][0]['bestLP']),(4,2))
        self.assertEqual(result['owned'],1)
        self.assertIsNone(result['items'][1]['bestLP'])

    def test_atlas_rejects_changed_owner(self):
        with patch.object(c.ipc,"request",return_value=dict(ok=True,player=dict(id='1'),items=[])),patch.object(c,"read_items",return_value=page([])),self.assertRaises(RuntimeError): c.atlas()

    def test_sets_do_not_claim_legendary_potential(self):
        with patch.object(c.ipc,"request",return_value=dict(ok=True,player=dict(id='6'),items=[dict(id=1,name='Set',set=True)])),patch.object(c,"read_items",return_value=dict(page([item(kind='Set',lp=0)]),pages=1,maxPageMs=0)):
            self.assertIsNone(c.atlas()['items'][0]['bestLP'])

    def test_uniques_on_same_base_are_not_duplicates(self):
        copies=[item(1),item(1,key='b'),item(2,key='c'),item(None,key='d'),item(None,key='e')]
        with patch.object(c,"read_items",return_value=page(copies)):
            result=c.stash()
        self.assertEqual(len(result['groups']),3)
        self.assertEqual(result['duplicateGroups'],1)

    def test_echo_parameters_reject_injection_before_ipc(self):
        with patch.object(m.progression,"request") as send:
            for sid,tid,emp,index in [('6\nexit',1,False,2),('6',True,False,2),('6',1,'normal',2),('6',1,False,-1)]:
                with self.assertRaises(ValueError):m.focus_echo(sid,tid,emp,index)
            send.assert_not_called()

    def test_echo_commands_are_single_explicit_requests(self):
        def reply(command):
            if command == 'monolithread':
                return dict(ok=True, restContextReady=True, player=dict(id='6', scene='M_Rest'))
            if command.startswith('monolithpanelready'):
                return dict(ok=True, panelReady=True, player=dict(id='6',scene='M_Rest'))
            return dict(ok=True)
        with patch.object(m.progression,"request",side_effect=reply) as send:
            m.echoes('6',1,False);m.focus_echo('6',1,True,2)
        self.assertEqual([a.args[0] for a in send.call_args_list],['echoread 6 1 normal','monolithread','monolithpanelready 6 1 empowered','echofocus 6 1 empowered 2'])


if __name__=='__main__':unittest.main()
