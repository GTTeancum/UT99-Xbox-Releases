

How to install and play AgentX 0.99
==================================================================

No Umod version currently available...coming soon

If you downloaded the manual installation files, unzip files  to your UnrealTournament\System directory.



For those of you who wish to setup a server, please refer to the bottom of this page.** 

Start Unreal Tournament. Begin a Practice Session or Server. Click the Mutators button. Drag or double-click the "AgentXarena" mutator to the "Used Mutators" box. Close the mutator window. Start a game type of your chioce..

Fixes
=====

Fixed third person weapon positioning (stills needs a little tuning)

Knifes and c4 temporarily removed (to many bugs atm)

New features
============
All Balistic weapons now offer the possibilty of head shots...

Automatic weapons (mp5,m4,ak47) accuracy is now dependant on player movement..(ie.. running gives 1/3 the accuracy.. crouching gives 2x the accuracy).. So no more running around spraying the m4 like a maniac..

Hopefuly this will increase the need for cover and use of tactics ..

Known Bugs.
===========

Bots still ignore mines that have landed...(dopy buggers)

Weapons Available 
===================

Silenced pistol (based on the walter ppk)
 
submachine gun (new model... based on the mp5)

assault rifle ( based on the m4)

grenade launcher ( normal fire single grenade secondary fire impact grenade -explodes on contact with any surface)

throwing Knife(need to be carrying bandolier to use)-removed..

sniper rifle ( redisign coming soon) 

Mine launcher (experimental  at this stage all new graphics to come)

Golden gun ( single shot kills - unless target is protect up to eyeballs)

Ak47 (well it's an Ak47 automatic rifle ..nuff said)

rocket launcher( primary fire -slow high damage large hit radius rocket
secondary fire- very fast rocket much lower damage)**

**.(not sure if the mod needs a rocket launcher.. your thoughts on a post card please.)
 
Items
=====
Bandolier -required for carrying certain items(  removed until items implemented)
kevlar vest....( does what it says on the box...ahem)

more to come.......
===================
a shotgun (not sure what kind yet)


***Jet pack (replaces the jump boots)

***(modified jump or full flight..? your thoughts..?)

licence to kill ( replaces the udamage)



plus more to be decided... 

Game modes
==========




NOTE: Some other mutators won't work in conjunction with the AgentX mutator for obvious reasons.


** To make an AgentX Server work 
=================================================

Open up your UnrealTournament.ini file in your UnrealTournament/System directory and look for the following heading:

[Engine.GameEngine]

At the end of that subsection are ServerPackages=

Simply add the following line after the rest of the ServerPackages lines:
ServerPackages=AgentX

If that line is already there, or appears more than once, ensure it's listed only once.

That's it!!!

