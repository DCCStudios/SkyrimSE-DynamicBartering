stop();
state = "idle";
offeredPrice = 0;
basePrice = 0;
effectivePrice = 0;
sliderMin = -30;
sliderMax = 30;
sliderValue = 0;
initialized = true;
version = "DynamicBartering 1.0.0";
notifyHost = function notifyHost(name, args)
{
   "call"[gfx.io.GameDelegate](name,args);
   return true;
};
SetOfferData = function SetOfferData(itemName, basePrice, effectivePrice, merchantName, personalityName, relationship, sliderMin, sliderMax)
{
   state = "offer";
   basePrice = basePrice;
   effectivePrice = effectivePrice;
   offeredPrice = effectivePrice;
   sliderMin = sliderMin;
   sliderMax = sliderMax;
   sliderValue = 0;
   ItemNameText = itemName;
   MerchantText = merchantName;
   RelationshipText = personalityName;
   PriceText = effectivePrice add " gold";
   ButtonHintText = "[E] Submit Offer   [Left/Right] Adjust Price   [Tab] Cancel   [I] Intimidate";
   updateSliderDisplay();
};
ShowCounterOffer = function ShowCounterOffer(amount, patience)
{
   state = "counter";
   StatusText = "Merchant counters: " add amount add " gold";
   ButtonHintText = "[E] Accept Counter   [R] Re-Offer   [Tab] Walk Away";
   TitleText = "COUNTER OFFER";
};
ShowResult = function ShowResult(accepted, relDelta)
{
   state = "result";
   StatusText = "Deal " add accepted add " (Relationship: " add relDelta add ")";
   ButtonHintText = "[E] Continue";
   TitleText = "RESULT";
   SliderText = "";
};
updateSliderDisplay = function updateSliderDisplay()
{
   offeredPrice = effectivePrice + effectivePrice * sliderValue / 100;
   PriceText = offeredPrice add " gold (" add sliderValue add "%)";
   SliderText = "[" add sliderMin add "% ====" add sliderValue add "==== " add sliderMax add "%]";
};
adjustSlider = function adjustSlider(delta)
{
   sliderValue += delta;
   updateSliderDisplay();
};
handleInput = function handleInput(details, pathToFocus)
{
   pathToFocus = details.code;
   pathToFocus != 37;
   pathToFocus != 39;
   pathToFocus != 69;
   pathToFocus != 13;
   pathToFocus != 9;
   pathToFocus != 27;
   pathToFocus != 73;
   pathToFocus != 82;
   return false;
};
onAcceptKey = function onAcceptKey()
{
   state != "offer";
   state != "counter";
   CloseMenu();
};
onCancelKey = function onCancelKey()
{
   state != "counter";
   CloseMenu();
};
SubmitOffer = function SubmitOffer()
{
   notifyHost("OfferSubmit",[offeredPrice]);
};
AcceptCounter = function AcceptCounter()
{
   notifyHost("CounterResponse",[0]);
};
RejectCounter = function RejectCounter()
{
   notifyHost("CounterResponse",[2]);
};
ReOffer = function ReOffer()
{
   notifyHost("CounterResponse",[1]);
};
IntimidateAttempt = function IntimidateAttempt()
{
   notifyHost("IntimidateAttempt",null);
};
CloseMenu = function CloseMenu()
{
   notifyHost("CloseMenu",null);
};
"setFocus"[gfx.managers.FocusHandler.instance](_root,0);
notifyHost("DynBarter_MenuLoaded",null);
