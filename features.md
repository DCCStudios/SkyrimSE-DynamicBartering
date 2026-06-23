# Silver Tongue — Dynamic Bartering

> Living feature document. This is the source of truth for the Nexus page description.
> Keep it updated as features are added, changed, or removed. Anything written here
> should reflect what the **shipped build actually does** — no aspirational features.

---

## The Pitch (intro blurb)

Buying and selling in Skyrim has always been the same boring ritual: open the menu, click the
item, watch the gold move. Your Speech skill is a number that quietly shaves a few coins off the
price and that's the end of the conversation.

I wanted shopping to feel like *bartering*. I wanted to be able to make an offer, watch the
merchant size me up, and either get talked down or talk them into it. I wanted the grumpy
blacksmith who hates me to charge more than the friendly innkeeper who's known me for fifty
hours. I wanted my Silver Tongue to actually *mean* something.

That's where this mod came from.

**Silver Tongue - Dynamic Bartering** turns every transaction into a real negotiation. You name
your price, the merchant reacts based on who they are and how they feel about you, and your Speech
skill, perks, and reputation all genuinely matter.

---

## Main Features

### Make an Offer — Real Haggling

Instead of paying whatever the menu says, you open a negotiation and **name your own price** with
a slider. The merchant then decides whether to accept, reject, or counter — and that decision is
driven by an actual chance calculation, not a coin flip.

What goes into whether they say yes:

- **How greedy your offer is.** Lowball them and your odds tank; offer at or above market and
  they'll almost always take it.
- **Your Speech skill** — a smooth talker is far more persuasive than a mute warrior.
- **Your Speech perks** (Haggling, Persuasion, Allure — see below).
- **Your relationship** with that specific merchant.
- **The merchant's personality** — some are pushovers, some are stone walls.
- **Your recent history** with them — string together fair deals and they warm up; keep
  lowballing and they stop trusting you.
- **Stolen goods** carry a penalty (softened heavily if you've taken the Fence perk).

<details><summary>Default numbers (all tunable in the MCM)</summary>

- Base acceptance: **50%**
- Full (100) Speech: up to **+20%**
- Haggling: **+5% per rank** (max +25% at rank 5)
- Persuasion: **+15%**
- Allure: **+10%** vs. opposite-gender merchants
- Relationship: up to **+15%** when you're trusted
- Consecutive fair deals: **+2% each** (cap +10%)
- Recent lowballs: **−3% each** (cap −15%)
- Stolen item: **−30%**, reduced by ~66% with the Fence perk
- An offer at or better than market price is treated as a near-guaranteed **accept**

</details>

### The Negotiation Window

A custom, vanilla-styled offer window built in Scaleform that drops you straight into the haggle:

- The merchant's **name**, **personality**, and a **colored relationship meter** so you always
  know where you stand.
- **Market price** up front, plus a live **"You Pay / You Receive"** readout with a coin icon as
  you move the slider.
- **Fine slider control on every input** — drag it with the mouse, nudge one gold at a time with
  the arrows / D-pad, or jump in steps of **5** with the shoulder buttons. On-screen hints show
  the right glyphs (LB/RB or L1/R1) for your controller.
- A **live verdict** that reads the room as you adjust your offer — from *"Merchant will ACCEPT"*
  in green all the way down to *"likely REFUSE"* in red — so you're never blindly guessing.
- A **relationship preview** that tells you how this particular offer will land: generous offers
  earn goodwill, insulting ones cost you.
- Clean **Accept / Reject** result screens that show exactly how the deal changed your standing.

### Counter-Offers

Merchants aren't passive. If they think they can squeeze you, they'll **counter** with a price of
their own — and you can **Accept**, **Re-offer**, or **Walk Away**. How hard they haggle and how
much they're willing to meet you in the middle depends on their personality. Greedy merchants
fight for every coin; generous ones fold quickly. Each merchant also has limited **patience** —
push too many rounds and the haggling runs out.

### Intimidation

Don't feel like talking? **Intimidate** them instead. Success scales off your Speech skill and
level, and the **Intimidation perk** doubles your odds. Pull it off and you buy at a steep
discount or sell at a premium — but strong-arming people **damages your standing**, and a failed
threat sours the relationship even more. High risk, high reward.

### The Barter Cart — Negotiate a Whole Pile at Once

Why haggle one item at a time? Tap the barter button to **toss items into a cart** — things you
want to buy *and* things you want to sell — then negotiate the **entire trade as a single deal**.

- A clean cart panel lists everything with per-item **+/− gold**.
- Running totals show **You Pay**, **You Receive**, or **Even Trade**.
- One negotiation settles the **net gold** across the whole basket — buy two daggers, sell an old
  shield, and barter the difference in one conversation.
- **Stackable items are easy to portion out.** Small stacks drop into the cart **one at a time**
  per tap (tap once more past the top of the stack to clear it), while larger stacks (more than 5)
  pop the vanilla **quantity slider** so you can pick exactly how many to add. Re-select a stacked
  item to **pull some back out** — added 12 and only want 8 gone? Pick 8 and 4 stay; pick the whole
  lot and it's removed.
- The cart **fits as much as you throw at it** — the list shrinks its text to keep everything
  visible, and once it's genuinely full it nudges you to finish the deal or remove something
  rather than silently overflowing.
- The cart is **visible the moment the barter menu opens** (with a helpful empty-state hint), and
  shows your **current standing as colored text** — e.g. *"Standing: 12% buy discount"* in green
  when a merchant favors you, or *"Standing: +18% markup"* in red when they don't — so the effect
  of your reputation is always in plain sight.
- The cart window can be **freely repositioned and scaled** from the MCM, live, while the barter
  menu is open.

**Don't trust your trigger finger?** Flip on **Block Quick Buy/Sell** in the MCM and the normal
activate button **adds to the cart** instead of buying on the spot — so a stray click can never
empty your purse. In this mode you tap to add and **hold the barter button to open the
negotiation**.

### Merchant Personalities

Every merchant has a personality that shapes how they trade. Around **80+ named vanilla
merchants** ship with hand-assigned dispositions, and unknown/modded merchants fall back to a
sensible default (Thieves Guild fences are automatically Sleazy).

<details><summary>The six personality types</summary>

- **Greedy** — hard to please, haggles aggressively, charges friends and strangers alike.
- **Fair** — the honest baseline.
- **Generous** — easygoing, accepts readily, barely holds a grudge.
- **Sleazy** — loves to haggle and never takes a lowball personally (great for fences).
- **Stern** — cold and unyielding, low patience, long memory for insults.
- **Timid** — nervous and accommodating, easy to talk up and easy to intimidate.

</details>

Modders/users can assign personalities to any merchant via simple JSON files in the
`Dispositions` folder.

### Relationships & Memory

Merchants **remember you**. Every shop has its own relationship score (−100 to +100) and its own
memory of your recent deals.

- Pay fairly and they warm up; lowball or intimidate them and they cool off.
- A history of **fair deals** makes them more agreeable over time; a streak of **lowballs** makes
  them wary.
- Standing changes surface through the relationship meter in the offer window and quick corner
  notifications, so the relationship feels alive.
- All of it is **saved to your co-save**, per merchant, so your reputation persists.

### Relationship-Driven Haggling

Your standing doesn't just nudge the acceptance odds — it **changes how good a deal you can
actually reach**. A merchant who likes you opens up real room: deeper discounts when you buy and
higher overcharges when you sell. A merchant who can't stand you holds firm, tightening the range
and even charging above market.

- Better standing both **lowers the base price** *and* **widens how far you can push** — the two
  reinforce each other. Poor standing does the reverse: a higher base price *and* less wiggle room.
- The effect is always **bounded by the merchant's personality** — a Greedy or Stern trader gives
  far less ground than a Generous or Sleazy one, no matter how much they love you.
- Caps keep it sane: a configurable **maximum buy discount** and **maximum sell markup** mean even
  your closest merchant friend won't give the shop away.
- The adjusted price can also be **reflected right in the vanilla item-card price**, so the number
  Skyrim shows you matches the gold you'll actually pay (see *Vanilla Price Reflection* below).

### Merchant Specialties

Merchants care more about the goods they **actually deal in**. A blacksmith will haggle happily
over an iron sword but balk at the same lowball on a rare enchanted necklace; an apothecary bends
on potions and ingredients; a court wizard warms to spell tomes and enchanted gear.

- Every merchant is sorted into a **category** — Blacksmith, General Goods, Apothecary, Court
  Wizard, Innkeeper, Clothier, Jeweler, Fence, Khajiit Caravan, or Generalist — and items are
  classified by type (weapons, armor, potions, ingredients, food, books, spell tomes, jewelry,
  enchanted gear, and more).
- Offers on **in-specialty goods are easier** to land; clearly **off-specialty items meet
  resistance**. The strength of this effect is fully tunable, and the whole system can be toggled
  off.
- Vanilla merchants ship **hand-categorized**, and modded/added merchants can be sorted via the
  same simple JSON files (a `"category"` field alongside their personality).

### Milestone Reputation

Big achievements ripple out across **whole merchant categories**, not just the ones you've
personally befriended:

- Become **Arch-Mage** of the College of Winterhold and magic traders across Skyrim think more of
  you.
- Take over as **Guild Master** of the Thieves Guild and fences warm up.
- Earn your place as **Harbinger** of the Companions and the blacksmiths take notice.
- Finish the **Bards College** questline and innkeepers raise a glass to you.

Each milestone applies its category-wide bonus **once**, retroactively (so it still counts if you
finished the quest before installing), and the result is saved to your co-save. Hold-based
Thane/Civil-War standing is planned as a follow-up. The whole system can be toggled off, and the
current category bonuses are visible in the MCM's Relationships tab.

### In-Game Tutorial

First-time players get a **two-step tutorial** that explains the mod as they encounter it:

- The first time you open a barter menu, a popup explains the **cart** and how to open the
  **negotiation window**.
- The first time you open the offer window, a second popup walks through the **sliders, offers,
  counter-offers, intimidation**, and how your **relationship and a merchant's specialty widen or
  tighten the deals you can reach**.

It's **on by default for new installs**, asks you to confirm each step, and **disables itself**
once you've seen both — so it never nags. You can replay it any time with the **"Show Tutorial"**
toggle in the MCM.

### Vanilla Price Reflection (optional)

The relationship/personality price adjustment can be shown **directly on the vanilla barter
menu's item card**, so the price Skyrim displays already reflects how the merchant feels about you
— no mental math. It's implemented as a pure-DLL Scaleform wrapper that **chains with other price
mods** (like Dynamic Pricing Framework) instead of fighting them, and it can be toggled off if you
prefer the effect to live only in the mod's own offer/cart window.

### AI NPC Reactions — CHIM (optional)

If you run **CHIM / HerikaServer**, your haggling stops being a private affair. After each
bartering visit the mod quietly hands CHIM a summary of what happened, and the merchant:

- **Comments out loud once you're done** — pleased when you've filled their coin purse, grudging
  when you've cleaned them out, and rattled when you've strong-armed them with a threat.
- **Remembers the visit** and can bring it up naturally in later AI conversations — the items, the
  gold that changed hands, your intimidation, your lowballing.

It's completely **self-contained and safe**: if CHIM isn't installed or its server isn't running,
the mod detects that and stays totally inert — no stalls, no errors, no spam. Because Skyrim mutes
NPC voice while a menu is open, the spoken remark lands right after you close the barter/dialogue
menus. Reaction cooldowns, the post-close send delay, an optional **SkyrimSouls** unpaused-window
mode, and memory-only logging are all tunable in the config menu.

### Quick Deals (when you just want to shop)

Not every purchase needs a debate. A **normal buy/sell still works instantly** at market price —
and now even those quick deals build a little goodwill (a market-price sale makes a merchant
appreciate your business). If you try to quick-buy something that's already sitting in your barter
cart, a small popup asks whether you meant to open the negotiation, buy it now, or cancel — no
accidental purchases.

### Speech Perk Integration

This isn't a parallel system bolted on beside vanilla — it **reads your actual Speech tree** and
rewards the perks you've invested in:

| Perk | What it does here |
|------|-------------------|
| **Haggling** (1–5) | Better acceptance odds and more room on the offer slider |
| **Persuasion** | A flat boost to your persuasiveness |
| **Allure** | Bonus when dealing with the opposite gender |
| **Intimidation** | Doubles your intimidation success |
| **Fence** | Slashes the penalty for selling stolen goods |
| **Investor** | Softens price hikes from merchants who dislike you |

### Full Controller, Keyboard & Mouse Support

Built for every input method from the ground up:

- **Gamepad:** the "Barter" prompt appears right next to the highlighted item, with proper
  **Xbox (Y)** or **PlayStation (△)** button glyphs — your choice in the MCM.
- **Keyboard:** keycap-style glyphs and full hotkey control.
- **Mouse:** drag the slider, click the buttons — it all just works.

- **Hold** the barter button to open a negotiation — the prompt fills as you hold.
- **Tap** to add/remove the highlighted item from the cart (the barter button, or the normal
  activate button when *Block Quick Buy/Sell* is enabled). The prompt shows the right glyph for
  whatever you're holding.
- In the offer window, the **shoulder buttons move the price slider in steps of 5** for fast
  adjustments.

### Fully Configurable (SKSE Menu Framework)

Tune the whole experience in-game through the SKSE Menu Framework, or edit the INI directly.
**Every option has a plain-language hover tooltip**, so you never have to guess what a setting does:

- **General** — enable/disable, **UI backend** and **gamepad glyph style** dropdowns, sound
  volume, **cart-visible-by-default** and **Show Tutorial** toggles, and the optional **CHIM
  integration** toggle with its reaction cooldowns, send delay, and SkyrimSouls option.
- **Cart** — Block Quick Buy/Sell toggle, hold-to-open timing (tap window + fill speed), plus live
  cart-window position and scale.
- **Pricing** — base acceptance, Speech weight, perk bonuses, greed sensitivity, stolen-goods
  penalty, the **relationship haggle-range weight** with **max buy-discount / sell-markup** caps,
  the **Merchant Specialties** toggle + weight, and the **vanilla-price reflection** toggle.
- **Relationships** — how much standing matters, the price-jack/price-break thresholds and the
  **Milestone Reputation** toggle, a per-merchant relationship **editor** that shows each
  merchant's standing, deal stats, detected **specialty**, and the **live price effect** of that
  standing (including any category bonus), plus a readout of your current **category reputation**.
- **Personalities** — counter-offer tuning.
- **Debug** — logging and data tools.

No SKSE Menu Framework? The mod still runs fine on its auto-generated INI.

---

## Compatibility

- **Skyrim SE / AE / VR** — single DLL, built on CommonLibVR-NG.
- **SkyUI friendly** — the barter prompt and cart logic are written to work with SkyUI's barter
  menu, and buy/sell detection uses the menu's own logic rather than fragile assumptions.
- Doesn't touch leveled lists, vendor inventories, or vanilla pricing records — it intercepts the
  barter menu at runtime, so it plays nicely with other shop/economy mods.
- Can be safely toggled off in the MCM (hooks pass straight through to vanilla).

---

## Requirements

| Requirement | Notes |
|-------------|-------|
| **SKSE64** | Hard requirement |
| **Address Library for SKSE Plugins** | Hard requirement |
| **SKSE Menu Framework** | Optional — needed only for the in-game config menu |
| **SkyUI** | Optional — supported but not required |
| **PrismaUI** | Optional — alternate UI backend |
| **CHIM / HerikaServer** | Optional — enables AI NPC voice reactions & memory of your haggling |
| **SkyrimSouls - Unpaused Game Menus** | Optional — lets the offer window stay unpaused (CHIM) |

---

## Installation

Install with your preferred mod manager. Add SKSE Menu Framework if you want the in-game settings
menu. That's it.

## Uninstallation

Can be removed at any time. Your merchant relationships live in the co-save, so removing the mod
simply stops the negotiation system — nothing is left baked into your world.

---

## Notes

Built with the help of AI. If that bothers you, no hard feelings — just skip it.

---

## Credits & Acknowledgements

This mod stands on the shoulders of the SKSE modding community. Huge thanks to:

**Core frameworks & tools**

- **SKSE64** — ianpatt, behippo, purplelunchbox, and the rest of the SKSE team. None of this is possible without it.
- **Address Library for SKSE Plugins** — meh321, for keeping plugins working across runtime versions.
- **CommonLibSSE-NG / CommonLibVR** — alandtse and contributors, building on **CommonLibSSE** by Ryan-rsm-McKenzie and the NG fork by CharmedBaryon. This is the backbone the plugin is built on.
- **SkyUI** — the SkyUI Team, whose barter menu the cart and prompt logic work alongside.

**Integrations (optional features)**

- **SKSE Menu Framework** — Thiago099, powering the in-game configuration menu.
- **PrismaUI** — its author, for the optional HTML UI backend.
- **CHIM / HerikaServer** — DwemerDynamics, enabling the AI-NPC voice reactions and merchant memory.
- **SkyrimSouls RE** — Vermunds, for the unpaused-menu support that lets the offer window stay live.

**Libraries**

- **spdlog** — Gabi Melman (gabime).
- **nlohmann/json** — Niels Lohmann.
- **SimpleIni** — Brodie Thiesfield (brofield).

**Reference & inspiration**

- **Dynamic Pricing Framework (DPF)** and **DynamicPrices-SKSE** — studied for the pure-DLL `UpdateItemCardInfo` Scaleform approach used to reflect relationship pricing in the vanilla item card; the wrapper is written to chain with these mods rather than conflict with them.
- **UESP** and the Elder Scrolls Fandom wiki — merchant lists used to hand-author the vanilla merchant personalities and specialty categories.

If you contributed something used here and aren't credited, it's an oversight, not a slight — let me know and I'll add you.

---

<!--
MAINTENANCE NOTES (not for the Nexus page):
- CREDITS: verify exact author handles before publishing to Nexus. Confirmed from local sources:
  SKSE Menu Framework = Thiago099, SkyrimSouls RE = Vermunds, CHIM/HerikaServer = DwemerDynamics.
  STILL TO CONFIRM: PrismaUI author handle, DynamicPrices-SKSE author, Dynamic Pricing Framework
  (DPF) author — their local copies had no embedded author name. Fill these in before release.
- Describe only features that are wired and verified in the live build.
- Several MCM/INI keys are placeholders (decay rate, popup delay, some rel gain/loss amounts,
  personality weight, value-threshold skip, acceptance/relationship hint toggles). Do NOT
  advertise these until they're actually wired into gameplay.
- Relationship now drives a BIDIRECTIONAL base-price multiplier (PriceJack::GetBuySellMultiplier):
  good standing = buy discount / sell bonus, poor standing = buy markup / sell penalty. This is
  applied ONCE on both the single-item (StartOffer) and cart (StartCartOffer) negotiation paths, so
  the cart path no longer neutralizes the effect. The Relationships-tab "Price effect" readout uses
  GetBuySellMultiplier with the EFFECTIVE relationship (per-merchant + category milestone offset)
  and applies on both paths.
- Vanilla item-card price reflection (showRelationshipInVanillaPrices) is a pure-DLL
  UpdateItemCardInfo Scaleform wrapper in Hooks.cpp that multiplies the displayed card value by the
  same buy/sell multiplier and chains to the original (so it cooperates with DPF / Dynamic Prices).
  The negotiation reads the raw list value and applies the multiplier itself, so the displayed card
  price matches the gold charged WITHOUT double-application (the wrapper only restyles the card
  display object, not the value the negotiation reads).
- CHIM integration is live: the DLL pushes a per-visit "session" summary to comm.php after the
  barter+dialogue menus close, and the CHIM-DynamicBartering ext plugin logs it to merchant memory
  and queues a spoken closing remark (responselog/ScriptQueue). The closing remark is gated only by
  cooldown + the server master switch, NOT by big_moment, so ordinary visits still get a comment.
- The CHIM "immediate spoken reactions" toggle is hidden from the menu; reactions are post-close.
- Update this file whenever features land.
-->
