<?php
/**
 * CHIM-DynamicBartering :: preprocessing.php
 *
 * Handles the `barter_event` pushed over HTTP by the DynamicBartering SKSE plugin.
 * Runs from main.php (~line 168) BEFORE the MAIN semaphore and BEFORE
 * processor/comm.php, so we do our work and terminate() without ever touching the
 * LLM request path.
 *
 * Wire payload (base64 of `barter_event|<ts>|<gamets>|<json>`):
 *   { merchant, merchant_id, personality, relationship, action, item,
 *     market_price, offered_price, gold_delta, is_buying, is_stolen, big_moment }
 *
 * What we do:
 *   1. ALWAYS log the outcome as an `infoaction` attributed to the merchant, so the
 *      NPC remembers it and naturally references it (LLM-voiced) next conversation.
 *   2. For "big moments", optionally queue an immediate in-character bark into
 *      `responselog`, which CHIM's own client speaks on its next `request` poll.
 */

// Only handle our event; let every other request fall through untouched.
if (($GLOBALS["gameRequest"][0] ?? "") !== "barter_event") {
    return;
}

/* ============================================================================== */
/* Helpers (defined first so they exist before the logic below calls them).        */
/* Guarded against redefinition since main.php require_once's this file.            */
/* ============================================================================== */

if (!function_exists('dynBarterQuote')) {
    function dynBarterQuote($s) {
        global $db;
        if (is_object($db) && method_exists($db, 'escape')) {
            return "'" . $db->escape($s) . "'";
        }
        return "'" . str_replace("'", "''", (string)$s) . "'";
    }
}

if (!function_exists('dynBarterPick')) {
    function dynBarterPick(array $options) {
        if (empty($options)) return "";
        return $options[array_rand($options)];
    }
}

/** Third-person, present-tense world-event line stored in the merchant's memory.
 *  Weaves in the item, the buy/sell direction, the agreed/offered gold and the
 *  item's market value so the LLM has concrete facts to react to.
 *  Returns the bare sentence(s) WITHOUT the wrapping parentheses, so it can be
 *  reused inside a multi-line session summary. */
if (!function_exists('dynBarterNarrativeLine')) {
    function dynBarterNarrativeLine($player, $merchant, $action, $item, $isBuying, $isStolen, $marketPrice, $offeredPrice, $counterPrice = 0) {
        $stolen  = $isStolen ? " The {$item} is stolen goods." : "";
        $market  = (int)$marketPrice;
        $paid     = (int)$offeredPrice;
        $counter  = (int)$counterPrice;

        $paidStr    = number_format($paid)    . " gold";
        $marketStr  = number_format($market)  . " gold";
        $counterStr = number_format($counter) . " gold";
        // Only mention the market value when we actually have one.
        $worth = $market > 0 ? " (worth about {$marketStr})" : "";

        switch ($action) {
            case "counter":
                $line = $isBuying
                    ? "{$player} offers {$paidStr} for {$item}{$worth}; {$merchant} counters, holding out for {$counterStr}."
                    : "{$player} asks {$paidStr} for {$item}{$worth}; {$merchant} counters, willing to pay only {$counterStr}.";
                break;
            case "intimidate_success":
                $line = $isBuying
                    ? "{$player} threatens {$merchant} into selling {$item}{$worth} for just {$paidStr}, strong-arming a steep discount."
                    : "{$player} threatens {$merchant} into paying {$paidStr} for {$item}{$worth}, forcing them to overpay.";
                break;
            case "intimidate_fail":
                $line = "{$player} tries to strong-arm {$merchant} over {$item}{$worth}, but the threat falls flat and {$merchant} is offended.";
                break;
            case "generous":
                $line = $isBuying
                    ? "{$player} pays {$merchant} {$paidStr} for {$item}, generously above its {$marketStr} value."
                    : "{$player} sells {$item} to {$merchant} for only {$paidStr}, well below its {$marketStr} value - a generous deal for the merchant.";
                break;
            case "lowball":
                $line = $isBuying
                    ? "{$player} haggles {$merchant} down to {$paidStr} for {$item}{$worth} and walks away with a bargain."
                    : "{$player} talks {$merchant} into paying {$paidStr} for {$item}{$worth}, squeezing out extra coin.";
                break;
            case "counter_reject":
                $line = $isBuying
                    ? "{$player} offers only {$paidStr} for {$item}{$worth} and {$merchant} flatly refuses."
                    : "{$player} demands {$paidStr} for {$item}{$worth} and {$merchant} flatly refuses.";
                break;
            case "counter_accept":
                $line = $isBuying
                    ? "{$player} and {$merchant} haggle over {$item}{$worth} and settle on {$paidStr}."
                    : "{$player} and {$merchant} haggle over {$item}{$worth} and settle on {$paidStr} for it.";
                break;
            case "walk_away":
                $line = "{$player} walks away from {$merchant} after they cannot agree on a price for {$item}{$worth}.";
                break;
            case "fair":
            case "deal_close":
            default:
                $line = $isBuying
                    ? "{$player} buys {$item} from {$merchant} for {$paidStr} (market value {$marketStr})."
                    : "{$player} sells {$item} to {$merchant} for {$paidStr} (market value {$marketStr}).";
                break;
        }
        return "{$line}{$stolen}";
    }
}

/** Wraps a single narrative line in parentheses for logging as an infoaction. */
if (!function_exists('dynBarterNarrative')) {
    function dynBarterNarrative($player, $merchant, $action, $item, $isBuying, $isStolen, $marketPrice, $offeredPrice, $counterPrice = 0) {
        return "(" . dynBarterNarrativeLine($player, $merchant, $action, $item, $isBuying, $isStolen, $marketPrice, $offeredPrice, $counterPrice) . ")";
    }
}

/** Consolidated memory entry for a whole barter visit (paused-menu / no-SkyrimSouls
 *  path). Joins each transaction's narrative line and closes with the net gold. */
if (!function_exists('dynBarterSessionNarrative')) {
    function dynBarterSessionNarrative($player, $merchant, $transactions, $netGold, $relationship = 0) {
        $lines = [];
        foreach ($transactions as $t) {
            $lines[] = dynBarterNarrativeLine(
                $player, $merchant,
                strtolower((string)($t["action"] ?? "")),
                trim((string)($t["item"] ?? "")) ?: "the goods",
                (bool)($t["is_buying"] ?? true),
                (bool)($t["is_stolen"] ?? false),
                (int)($t["market_price"] ?? 0),
                (int)($t["offered_price"] ?? 0),
                (int)($t["counter_price"] ?? 0)
            );
        }
        $net = (int)$netGold;
        if ($net > 0) {
            $tally = " Over the visit, {$player} spent " . number_format($net) . " gold with {$merchant}.";
        } elseif ($net < 0) {
            $tally = " Over the visit, {$merchant} paid {$player} " . number_format(abs($net)) . " gold.";
        } else {
            $tally = "";
        }
        $body = implode(" ", $lines);
        $standing = dynBarterStandingPhrase($player, $merchant, $relationship);
        $standingNote = $standing !== "" ? " At present, {$standing}." : "";
        return "(During this visit to {$merchant}: {$body}{$tally}{$standingNote})";
    }
}

/** Maps the mod's roughly -100..+100 relationship score to a coarse standing tier:
 *  -3 despised, -2 hostile, -1 cold, 0 neutral, +1 warm, +2 friendly, +3 beloved. */
if (!function_exists('dynBarterStandingTier')) {
    function dynBarterStandingTier($rel) {
        $r = (int)$rel;
        if ($r <= -75) return -3;
        if ($r <= -40) return -2;
        if ($r <= -15) return -1;
        if ($r <   15) return 0;
        if ($r <   40) return 1;
        if ($r <   75) return 2;
        return 3;
    }
}

/** Third-person clause describing how the merchant currently feels about the player,
 *  woven into the memory narrative so the LLM voices later lines with the right warmth
 *  (this is what keeps a despised merchant from greeting the player like an old friend). */
if (!function_exists('dynBarterStandingPhrase')) {
    function dynBarterStandingPhrase($player, $merchant, $rel) {
        switch (dynBarterStandingTier($rel)) {
            case -3: return "{$merchant} despises {$player} and can barely stand to deal with them";
            case -2: return "{$merchant} is hostile toward {$player} and resents the dealing";
            case -1: return "{$merchant} is cold and wary toward {$player}";
            case  1: return "{$merchant} is warming to {$player}";
            case  2: return "{$merchant} considers {$player} a valued, friendly customer";
            case  3: return "{$merchant} adores {$player} and treasures their custom";
            default: return "";  // neutral -> no special note
        }
    }
}

/** A single closing remark for the merchant after a whole visit (deferred path).
 *  Tone shifts with their personality, whether they came out ahead, AND - crucially -
 *  how they feel about the player: a merchant who dislikes the player stays curt even
 *  on a profitable sale, while a beloved one is warmer than usual. */
if (!function_exists('dynBarterSessionBark')) {
    function dynBarterSessionBark($merchant, $personality, $netGold, $hadIntimidation, $relationship = 0) {
        $p = strtolower($personality);
        if ($hadIntimidation) {
            if ($p === "stern")  return dynBarterPick(["Take your goods and don't threaten me again.", "We're done here. Go."]);
            if ($p === "timid")  return dynBarterPick(["P-please, just take it and go...", "I-I hope that's everything..."]);
            return dynBarterPick(["...Just go. We're done.", "Don't make a habit of strong-arming me."]);
        }

        $tier = dynBarterStandingTier($relationship);
        $net = (int)$netGold;

        // Disliked merchants (cold/hostile/despised) never warm up, even when the player
        // spent coin - that mismatch ("pleasure as always, friend" while despised) is the
        // exact thing the standing tier is here to prevent.
        if ($tier <= -2) {
            return dynBarterPick(["Hmph. Take your things and go.", "Our business is done. Don't linger.",
                                  "Don't expect a smile with it.", "Just go."]);
        }
        if ($tier === -1) {
            return dynBarterPick(["...That's everything, then.", "Good day to you.", "We're done here."]);
        }

        if ($net > 0) {  // player spent coin -> merchant pleased (and warmer if liked)
            if ($tier >= 2) return dynBarterPick(["Always a joy doing business with you, friend!", "For you? A pleasure, truly. Come back soon!"]);
            if ($p === "greedy") return dynBarterPick(["Ha! Always a pleasure taking your coin.", "Pleasure doing business. Come back soon!"]);
            if ($p === "sleazy") return dynBarterPick(["Heh, pleasure as always, friend.", "Spend it while you've got it, eh?"]);
            return dynBarterPick(["A pleasure doing business with you.", "Thank you kindly. Come again."]);
        }
        if ($net < 0) {  // merchant paid out -> grudging (unless they truly adore the player)
            if ($tier >= 2) return dynBarterPick(["For a friend, it's worth it. Come again.", "You always know how to find a deal with me."]);
            if ($p === "greedy") return dynBarterPick(["You drive a hard bargain, I'll give you that.", "Bah. You've cleaned out my coin purse."]);
            if ($p === "stern")  return dynBarterPick(["Hmph. Don't expect deals like that often.", "You got your coin. That's enough."]);
            return dynBarterPick(["You've a sharp tongue for trade.", "A fair bit of my coin, that was."]);
        }
        return "";  // neutral net -> no closing bark
    }
}

/**
 * Short first-person line for the merchant to say immediately (templated for
 * reliability; the LLM-voiced depth comes from the logged memory on the next chat).
 */
if (!function_exists('dynBarterBarkLine')) {
    function dynBarterBarkLine($merchant, $personality, $action, $item, $isBuying, $counterPrice = 0, $relationship = 0) {
        $p = strtolower($personality);
        $tier = dynBarterStandingTier($relationship);

        // A merchant who dislikes the player stays cold on otherwise-warm outcomes
        // (a generous overpayment or a fair close) instead of gushing thanks.
        if ($tier <= -2 && ($action === "generous" || $action === "fair" || $action === "deal_close" || $action === "counter_accept")) {
            return dynBarterPick(["Hmph. Coin's coin, I suppose.", "Don't think this changes anything between us.", "Take it and go."]);
        }

        if ($action === "counter") {
            $c = number_format((int)$counterPrice) . " gold";
            if ($p === "greedy") return dynBarterPick(["That barely covers my costs. {$c}, and not a coin less.", "Do I look like a charity? {$c}."]);
            if ($p === "generous") return dynBarterPick(["I can't quite go that low, but I'll meet you at {$c}.", "Tell you what - {$c} and we both walk away happy."]);
            if ($p === "sleazy") return dynBarterPick(["Heh, for a face like yours? {$c}.", "Let's not haggle all day. {$c} and it's done."]);
            if ($p === "stern") return dynBarterPick(["No. {$c}. That's my price.", "{$c}. Take it or leave it."]);
            if ($p === "timid") return dynBarterPick(["Oh, um... could we maybe do {$c} instead?", "I-I really need {$c} for it, if that's alright..."]);
            return dynBarterPick(["That's a bit steep for me. How about {$c}?", "I was thinking more like {$c}."]);
        }
        if ($action === "intimidate_success") {
            if ($p === "timid")  return dynBarterPick(["Alright, alright! Just... please, take it.", "Okay! Okay! No need for that, it's yours."]);
            if ($p === "stern")  return dynBarterPick(["You think threats move me? ...Fine. Take it and go.", "Hmph. This once. Don't come back."]);
            if ($p === "greedy") return dynBarterPick(["You drive a hard bargain... and a frightening one. Fine.", "Bah! Take it before I change my mind."]);
            return dynBarterPick(["...Fine. No need for trouble. It's yours.", "Easy now. Let's just call it done."]);
        }
        if ($action === "intimidate_fail") {
            if ($p === "stern")  return dynBarterPick(["Threaten me again and you'll regret it.", "You don't scare me, sellsword."]);
            if ($p === "timid")  return dynBarterPick(["G-guards! ...No? Well, the answer is still no.", "Please, just leave if you're going to act like that."]);
            return dynBarterPick(["Try that again and we're done here.", "I don't take kindly to threats."]);
        }
        if ($action === "generous") {
            if ($p === "greedy") return dynBarterPick(["Ha! Now THAT'S a customer I like.", "Pleasure doing business with you, truly."]);
            return dynBarterPick(["That's more than fair of you. My thanks.", "Generous! You're welcome here any time."]);
        }
        if ($action === "lowball" || $action === "counter_reject") {
            if ($p === "sleazy") return dynBarterPick(["Heh, can't fault you for trying.", "Cheeky. I respect the hustle."]);
            if ($p === "stern")  return dynBarterPick(["That's an insult, not an offer.", "Don't waste my time with that."]);
            return dynBarterPick(["You'll have to do better than that.", "Come now, I have to eat too."]);
        }
        return "";
    }
}

if (!function_exists('dynBarterReactionAllowed')) {
    function dynBarterReactionAllowed($merchant, $action, $cooldown) {
        global $db;
        $isCounter = ($action === "counter");
        $cooldown = (int)$cooldown;
        if ($cooldown <= 0) return true;
        try {
            // Throttle counters only against prior counters, and other big moments
            // only against prior non-counter big moments, so the two don't mute each other.
            $actionClause = $isCounter ? "action = 'counter'" : "action <> 'counter'";
            $row = $db->fetchOne(
                "SELECT MAX(localts) AS last FROM barter_events WHERE big_moment = TRUE AND {$actionClause} AND merchant = " . dynBarterQuote($merchant)
            );
            if ($row && !empty($row["last"]) && (time() - (int)$row["last"]) < $cooldown) {
                return false;
            }
        } catch (\Throwable $e) {
            // Table may not exist yet (migration not run); don't block reactions.
        }
        return true;
    }
}

if (!function_exists('dynBarterRecordRow')) {
    function dynBarterRecordRow($evt, $narrative) {
        global $db;
        try {
            $db->insert('barter_events', [
                'localts'       => time(),
                'merchant'      => (string)($evt["merchant"] ?? ""),
                'merchant_id'   => (string)($evt["merchant_id"] ?? ""),
                'personality'   => (string)($evt["personality"] ?? ""),
                'relationship'  => (int)($evt["relationship"] ?? 0),
                'action'        => (string)($evt["action"] ?? ""),
                'item'          => (string)($evt["item"] ?? ""),
                'market_price'  => (int)($evt["market_price"] ?? 0),
                'offered_price' => (int)($evt["offered_price"] ?? 0),
            'gold_delta'    => (int)($evt["gold_delta"] ?? 0),
            // pg_query_params stringifies PHP false to "" which Postgres rejects for a
            // BOOLEAN column, so map to explicit 't'/'f' literals.
            'is_buying'     => !empty($evt["is_buying"]) ? 't' : 'f',
            'is_stolen'     => !empty($evt["is_stolen"]) ? 't' : 'f',
            'big_moment'    => !empty($evt["big_moment"]) ? 't' : 'f',
            'narrative'     => (string)$narrative,
            ]);
        } catch (\Throwable $e) {
            // Best-effort analytics; never block the event on it.
        }
    }
}

/**
 * Queue a spoken line into responselog. CHIM's client dequeues it on its next
 * `request` poll and echoes "{actor}|ScriptQueue|{text}" - the same shape the normal
 * speech path produces:
 *   subtitle / expression / listener / animation / phonetic / volume / rechat / utteranceId
 */
if (!function_exists('dynBarterQueueBark')) {
    function dynBarterQueueBark($merchant, $line) {
        global $db;
        $uid = "barter_" . uniqid();
        $text = $line . "////" . $line . "/1//" . $uid;
        try {
            $db->insert('responselog', [
                'localts' => time(),
                'sent'    => 0,
                'actor'   => $merchant,
                'text'    => $text,
                'action'  => 'ScriptQueue',
                'tag'     => 'dynbarter',
            ]);
        } catch (\Throwable $e) {
            if (class_exists('Logger')) {
                Logger::warn("[DynBarter] failed to queue bark: " . $e->getMessage());
            }
        }
    }
}

/* ============================================================================== */
/* Main handling                                                                   */
/* ============================================================================== */

$gameRequest = $GLOBALS["gameRequest"];

// main.php split the decoded data on "|". Rejoin everything from field 3 onward so a
// pipe inside the JSON (e.g. an item name) can't truncate the payload.
$payloadRaw = isset($gameRequest[3]) ? implode("|", array_slice($gameRequest, 3)) : "";
$evt = json_decode($payloadRaw, true);
if (!is_array($evt)) {
    if (class_exists('Logger')) {
        Logger::warn("[DynBarter] unparseable barter_event payload: " . substr($payloadRaw, 0, 200));
    }
    terminate();
    return;
}

$merchant     = trim((string)($evt["merchant"] ?? "")) ?: "the merchant";
$personality  = (string)($evt["personality"] ?? "");
$relationship = (int)($evt["relationship"] ?? 0);
$action       = strtolower((string)($evt["action"] ?? ""));
$item         = trim((string)($evt["item"] ?? "")) ?: "the goods";
$marketPrice  = (int)($evt["market_price"] ?? 0);
$offeredPrice = (int)($evt["offered_price"] ?? 0);
$counterPrice = (int)($evt["counter_price"] ?? 0);
$isBuying     = (bool)($evt["is_buying"] ?? true);
$isStolen     = (bool)($evt["is_stolen"] ?? false);
$bigMoment    = (bool)($evt["big_moment"] ?? false);

$playerName = $GLOBALS["PLAYER_NAME"] ?? "the player";

// --- Session summary path -----------------------------------------------------------
// Sent once when the barter menu closes (and the game unpauses) on setups WITHOUT
// SkyrimSouls, where per-event reactions couldn't be voiced while the menu paused the
// game. Consolidates every transaction of the visit into one memory entry plus a
// single closing remark.
if ($action === "session") {
    $transactions = (isset($evt["transactions"]) && is_array($evt["transactions"])) ? $evt["transactions"] : [];
    if (empty($transactions)) {
        terminate();
        return;
    }
    $netGold = (int)($evt["net_gold"] ?? 0);

    $sessNarr = dynBarterSessionNarrative($playerName, $merchant, $transactions, $netGold, $relationship);
    logEvent([
        "infoaction",
        $gameRequest[1] ?? time(),
        0,
        $sessNarr,
        "pending",
    ], $merchant);

    $hadIntimidation = false;
    foreach ($transactions as $t) {
        if (strtolower((string)($t["action"] ?? "")) === "intimidate_success") {
            $hadIntimidation = true;
            break;
        }
    }

    $reactionCd = isset($evt["reaction_cooldown"]) ? (int)$evt["reaction_cooldown"]
                                                   : (int)($GLOBALS["DYNBARTER_REACTION_COOLDOWN"] ?? 20);
    // The closing remark after a visit is the mod's primary spoken reaction, so it is
    // NOT gated on $bigMoment - any visit where money changed hands earns a comment
    // (dynBarterSessionBark returns "" for a neutral net, so quiet visits stay silent).
    // Still honour the server master switch and the per-merchant cooldown.
    $wantSessionBark = !empty($GLOBALS["DYNBARTER_IMMEDIATE_REACTIONS"])
        && dynBarterReactionAllowed($merchant, "session", $reactionCd);

    dynBarterRecordRow($evt, $sessNarr);

    if ($wantSessionBark) {
        $line = dynBarterSessionBark($merchant, $personality, $netGold, $hadIntimidation, $relationship);
        if ($line !== "") {
            dynBarterQueueBark($merchant, $line);
        }
    }

    terminate();
    return;
}

// 1) Narrative -> merchant memory (infoaction, gamets 0 => server uses last-known time).
//    Append the current standing so the LLM voices the memory with the right warmth.
$narrative = dynBarterNarrative($playerName, $merchant, $action, $item, $isBuying, $isStolen, $marketPrice, $offeredPrice, $counterPrice);
$standingPhrase = dynBarterStandingPhrase($playerName, $merchant, $relationship);
if ($standingPhrase !== "") {
    $narrative = rtrim($narrative, ")") . " At present, {$standingPhrase}.)";
}
logEvent([
    "infoaction",
    $gameRequest[1] ?? time(),
    0,
    $narrative,
    "pending",
], $merchant);

// 2) Decide on an immediate bark BEFORE recording this row, so the cooldown window
//    only counts *prior* big moments. Cooldowns come from the SKSE-menu values sent
//    in the payload, falling back to the server globals when absent.
$cooldown = ($action === "counter")
    ? (isset($evt["counter_cooldown"])  ? (int)$evt["counter_cooldown"]  : (int)($GLOBALS["DYNBARTER_COUNTER_COOLDOWN"]  ?? 6))
    : (isset($evt["reaction_cooldown"]) ? (int)$evt["reaction_cooldown"] : (int)($GLOBALS["DYNBARTER_REACTION_COOLDOWN"] ?? 20));
$wantBark = $bigMoment
    && !empty($GLOBALS["DYNBARTER_IMMEDIATE_REACTIONS"])
    && dynBarterReactionAllowed($merchant, $action, $cooldown);

dynBarterRecordRow($evt, $narrative);

if ($wantBark) {
    $line = dynBarterBarkLine($merchant, $personality, $action, $item, $isBuying, $counterPrice, $relationship);
    if ($line !== "") {
        dynBarterQueueBark($merchant, $line);
    }
}

terminate();
