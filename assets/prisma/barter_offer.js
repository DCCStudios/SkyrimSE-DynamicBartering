(function() {
    'use strict';

    let currentData = null;
    let currentOffer = 0;
    let state = 'idle'; // idle, offering, counter, result

    const elements = {};
    function $(id) { return document.getElementById(id); }

    function init() {
        elements.overlay = $('barter-overlay');
        elements.merchantName = $('merchant-name');
        elements.personality = $('merchant-personality');
        elements.relValue = $('relationship-value');
        elements.relText = $('relationship-text');
        elements.itemName = $('item-name');
        elements.basePrice = $('base-price');
        elements.offerPrice = $('offer-price');
        elements.slider = $('offer-slider');
        elements.sliderPct = $('slider-percentage');
        elements.sliderMinLabel = $('slider-min-label');
        elements.sliderMaxLabel = $('slider-max-label');
        elements.acceptFill = $('acceptance-fill');
        elements.acceptText = $('acceptance-text');
        elements.priceJackWarning = $('price-jack-warning');
        elements.dealHistory = $('deal-history');
        elements.historyList = $('history-list');
        elements.counterPanel = $('counter-offer-panel');
        elements.counterPrice = $('counter-price');
        elements.patienceDots = $('patience-dots');
        elements.resultOverlay = $('result-overlay');
        elements.resultText = $('result-text');
        elements.resultDelta = $('result-delta');
        elements.btnOffer = $('btn-offer');
        elements.btnIntimidate = $('btn-intimidate');
        elements.btnCancel = $('btn-cancel');
        elements.btnAcceptCounter = $('btn-accept-counter');
        elements.btnReoffer = $('btn-reoffer');
        elements.btnWalkaway = $('btn-walkaway');
        elements.perkSummary = $('perk-summary');

        elements.slider.addEventListener('input', onSliderChange);
        elements.btnOffer.addEventListener('click', onMakeOffer);
        elements.btnCancel.addEventListener('click', onCancel);
        elements.btnIntimidate.addEventListener('click', onIntimidate);
        elements.btnAcceptCounter.addEventListener('click', () => onCounterResponse(0));
        elements.btnReoffer.addEventListener('click', () => onCounterResponse(1));
        elements.btnWalkaway.addEventListener('click', () => onCounterResponse(2));

        document.addEventListener('keydown', onKeyDown);
    }

    function onKeyDown(e) {
        if (state === 'idle') return;

        if (e.key === 'e' || e.key === 'E') {
            if (state === 'offering') onMakeOffer();
            else if (state === 'counter') onCounterResponse(0);
            else if (state === 'result') onCancel();
        } else if (e.key === 'Tab' || e.key === 'Escape') {
            e.preventDefault();
            onCancel();
        } else if (e.key === 'i' || e.key === 'I') {
            if (currentData && currentData.hasIntimidation) onIntimidate();
        } else if (e.key === 'ArrowLeft') {
            elements.slider.value = parseInt(elements.slider.value) - 1;
            onSliderChange();
        } else if (e.key === 'ArrowRight') {
            elements.slider.value = parseInt(elements.slider.value) + 1;
            onSliderChange();
        }
    }

    function onSliderChange() {
        if (!currentData) return;
        let pct = parseInt(elements.slider.value);
        let offset = (pct / 100.0) * currentData.effectivePrice;
        currentOffer = Math.round(currentData.effectivePrice + offset);
        if (currentOffer < 1) currentOffer = 1;

        elements.offerPrice.textContent = currentOffer + ' gold';
        elements.sliderPct.textContent = (pct >= 0 ? '+' : '') + pct + '%';

        updateAcceptanceBar(pct);
    }

    function updateAcceptanceBar(pct) {
        let baseChance = currentData.acceptanceChance || 50;
        let discountPenalty = 0;
        if (pct < 0) {
            discountPenalty = Math.abs(pct) * 1.5;
        } else {
            discountPenalty = -pct * 0.5;
        }
        let estimatedChance = Math.max(0, Math.min(99, baseChance - discountPenalty));
        elements.acceptFill.style.width = estimatedChance + '%';
        elements.acceptText.textContent = Math.round(estimatedChance) + '%';

        if (estimatedChance > 70) {
            elements.acceptText.style.color = '#50b050';
        } else if (estimatedChance > 40) {
            elements.acceptText.style.color = '#d4b054';
        } else {
            elements.acceptText.style.color = '#b05050';
        }
    }

    function getRelationshipText(value) {
        if (value >= 60) return 'Trusted';
        if (value >= 30) return 'Friendly';
        if (value >= 10) return 'Warm';
        if (value >= -10) return 'Neutral';
        if (value >= -30) return 'Cool';
        if (value >= -60) return 'Hostile';
        return 'Despised';
    }

    function populateDealHistory(dealsJson) {
        if (!dealsJson || dealsJson === '[]') {
            elements.dealHistory.classList.add('hidden');
            return;
        }

        try {
            let deals = JSON.parse(dealsJson);
            if (deals.length === 0) {
                elements.dealHistory.classList.add('hidden');
                return;
            }

            elements.historyList.innerHTML = '';
            let displayCount = Math.min(5, deals.length);
            for (let i = deals.length - 1; i >= deals.length - displayCount; i--) {
                let d = deals[i];
                let li = document.createElement('li');
                let status = d.accepted ? '✓' : (d.wasCounterOffer ? '↔' : '✗');
                let ratio = d.basePrice > 0 ? Math.round(d.offeredPrice / d.basePrice * 100) : 100;
                li.textContent = status + ' ' + d.itemName + ' - ' + ratio + '% of value';
                elements.historyList.appendChild(li);
            }
            elements.dealHistory.classList.remove('hidden');
        } catch(e) {
            elements.dealHistory.classList.add('hidden');
        }
    }

    // PrismaUI Interop - called from C++
    window.ShowOffer = function(jsonStr) {
        let data = JSON.parse(jsonStr);
        currentData = data;
        state = 'offering';

        elements.merchantName.textContent = data.merchantName;
        elements.personality.textContent = data.personality;
        elements.relValue.textContent = data.relationship;
        elements.relText.textContent = getRelationshipText(data.relationship);
        elements.itemName.textContent = data.itemName;
        elements.basePrice.textContent = data.effectivePrice + ' gold';

        let sliderMin = Math.round(data.sliderMin * 100);
        let sliderMax = Math.round(data.sliderMax * 100);
        elements.slider.min = sliderMin;
        elements.slider.max = sliderMax;
        elements.slider.value = 0;
        elements.sliderMinLabel.textContent = sliderMin + '%';
        elements.sliderMaxLabel.textContent = '+' + sliderMax + '%';

        currentOffer = data.effectivePrice;
        elements.offerPrice.textContent = currentOffer + ' gold';
        elements.sliderPct.textContent = '0%';

        updateAcceptanceBar(0);

        if (data.hasIntimidation) {
            elements.btnIntimidate.classList.remove('hidden');
        } else {
            elements.btnIntimidate.classList.add('hidden');
        }

        if (data.priceJackMult > 1.0) {
            elements.priceJackWarning.textContent = 'Merchant seems displeased with you (prices +' +
                Math.round((data.priceJackMult - 1.0) * 100) + '%)';
            elements.priceJackWarning.classList.remove('hidden');
        } else {
            elements.priceJackWarning.classList.add('hidden');
        }

        elements.perkSummary.textContent = data.perkBonuses || '';
        populateDealHistory(data.dealHistory);

        elements.counterPanel.classList.add('hidden');
        elements.resultOverlay.classList.add('hidden');
        elements.overlay.classList.remove('hidden');
    };

    window.ShowCounter = function(jsonStr) {
        let data = JSON.parse(jsonStr);
        state = 'counter';

        elements.counterPrice.textContent = data.counter;

        let dots = '';
        for (let i = 0; i < data.patience; i++) dots += '●';
        for (let i = data.patience; i < 3; i++) dots += '○';
        elements.patienceDots.textContent = dots;

        elements.counterPanel.classList.remove('hidden');
        elements.btnOffer.classList.add('hidden');
    };

    window.ShowResult = function(jsonStr) {
        let data = JSON.parse(jsonStr);
        state = 'result';

        elements.resultText.textContent = data.accepted ? 'ACCEPTED' : 'REJECTED';
        elements.resultText.className = 'result-text ' + (data.accepted ? 'accepted' : 'rejected');

        let deltaStr = '';
        if (data.relDelta > 0) deltaStr = 'Relationship +' + data.relDelta;
        else if (data.relDelta < 0) deltaStr = 'Relationship ' + data.relDelta;
        elements.resultDelta.textContent = deltaStr;

        elements.resultOverlay.classList.remove('hidden');

        setTimeout(function() {
            hideUI();
        }, 2000);
    };

    function onMakeOffer() {
        if (state !== 'offering') return;
        let payload = JSON.stringify({ offeredPrice: currentOffer });
        try { window.BarterResult(payload); } catch(e) {}
    }

    function onCancel() {
        hideUI();
        try { window.BarterResult(JSON.stringify({ offeredPrice: -1 })); } catch(e) {}
    }

    function onIntimidate() {
        try { window.IntimidateAttempt('{}'); } catch(e) {}
    }

    function onCounterResponse(response) {
        state = 'offering';
        elements.counterPanel.classList.add('hidden');
        elements.btnOffer.classList.remove('hidden');

        try { window.CounterResponse(JSON.stringify({ response: response })); } catch(e) {}
    }

    function hideUI() {
        state = 'idle';
        currentData = null;
        elements.overlay.classList.add('hidden');
        elements.counterPanel.classList.add('hidden');
        elements.resultOverlay.classList.add('hidden');
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
