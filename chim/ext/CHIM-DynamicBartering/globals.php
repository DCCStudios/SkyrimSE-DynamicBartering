<?php
/**
 * CHIM-DynamicBartering :: globals.php
 *
 * Loaded very early by main.php (before the fast-command list is built and before
 * the MAIN semaphore is acquired). We register our custom game event `barter_event`
 * as an "external fast command" so HerikaServer routes it without spinning up the
 * heavy LLM request path. The real work happens in preprocessing.php.
 */

if (!isset($GLOBALS["external_fast_commands"]) || !is_array($GLOBALS["external_fast_commands"])) {
    $GLOBALS["external_fast_commands"] = [];
}
if (!in_array("barter_event", $GLOBALS["external_fast_commands"], true)) {
    $GLOBALS["external_fast_commands"][] = "barter_event";
}

/* ---- Tunables (override in conf/conf.php if desired) -------------------------- */

// Queue an immediate, in-character spoken bark when a "big moment" arrives
// (successful/failed intimidation, insulting lowball, generous overpay).
if (!isset($GLOBALS["DYNBARTER_IMMEDIATE_REACTIONS"])) {
    $GLOBALS["DYNBARTER_IMMEDIATE_REACTIONS"] = true;
}

// Minimum seconds between immediate barks for the same merchant (anti-spam).
// Applies to the "big" reactions (intimidation, generous overpay, insulting lowball).
if (!isset($GLOBALS["DYNBARTER_REACTION_COOLDOWN"])) {
    $GLOBALS["DYNBARTER_REACTION_COOLDOWN"] = 20;
}

// Counter-offers get their own, shorter cooldown so a merchant can justify each
// counter during a fast multi-round haggle without being muted by the cooldown above.
if (!isset($GLOBALS["DYNBARTER_COUNTER_COOLDOWN"])) {
    $GLOBALS["DYNBARTER_COUNTER_COOLDOWN"] = 6;
}
