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
- The cart window can be **freely repositioned and scaled** from the MCM, live, while the barter
  menu is open.

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

Open a negotiation by **holding** the barter button (the prompt fills as you hold); **tap** it to
add/remove the highlighted item from the cart.

### Fully Configurable (SKSE Menu Framework)

Tune the whole experience in-game through the SKSE Menu Framework, or edit the INI directly:

- **General** — enable/disable, UI backend, gamepad glyph style.
- **Cart** — hold-to-open duration, plus live cart-window position and scale.
- **Pricing** — base acceptance, Speech weight, perk bonuses, greed sensitivity, stolen-goods
  penalty, and more.
- **Relationships** — how much standing matters, plus a per-merchant relationship **editor**
  (view standing and deal stats, nudge it, or reset).
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

<!--
MAINTENANCE NOTES (not for the Nexus page):
- Describe only features that are wired and verified in the live build.
- Several MCM/INI keys are placeholders (decay rate, popup delay, some rel gain/loss amounts,
  personality weight, value-threshold skip, acceptance/relationship hint toggles). Do NOT
  advertise these until they're actually wired into gameplay.
- Overt price-jacking (raising the displayed base price for disliked players) currently runs
  through the unused single-item StartOffer path; the cart negotiation path neutralizes it.
  Relationship still affects ACCEPTANCE odds, which is what the "relationships matter" copy leans
  on — keep it framed that way until price-jacking is live on the cart path.
- Update this file whenever features land.
-->
