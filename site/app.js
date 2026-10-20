(() => {
  "use strict";
  const ideas = {
    counter: { title: "Small idea. Satisfying click.", plan: "A fullscreen counter with big, readable numbers. Cross adds one; Circle brings it back to zero." },
    timer: { title: "A little room to focus.", plan: "A calm focus timer with a big countdown. Start when you're ready, pause when you need to, and keep the screen simple." },
    pixels: { title: "Start with a single pixel.", plan: "A tiny canvas for colorful experiments. Choose a square, make a mark, and turn a blank grid into something yours." }
  };
  let selected = "counter", buildTimers = [], interval = null, busy = false;
  const stage = document.querySelector("#demo-stage"), title = document.querySelector("#demo-plan-title"), plan = document.querySelector("#demo-plan");
  const approve = document.querySelector("#demo-approve"), result = document.querySelector("#demo-result");
  const choices = [...document.querySelectorAll("[data-idea]")];
  function cleanup() {
    buildTimers.forEach(clearTimeout); buildTimers = [];
    if (interval) clearInterval(interval);
    interval = null; busy = false;
  }
  function reset() {
    cleanup(); result.replaceChildren(); result.hidden = true;
    title.textContent = ideas[selected].title; plan.textContent = ideas[selected].plan;
    stage.textContent = "Plan ready for review";
    approve.disabled = false; approve.textContent = "Approve demo ×";
    choices.forEach(button => { button.disabled = false; button.setAttribute("aria-pressed", String(button.dataset.idea === selected)); });
  }
  function button(label, action) {
    const element = document.createElement("button"); element.type = "button";
    element.textContent = label; element.addEventListener("click", action); return element;
  }
  function showResult() {
    stage.textContent = "Browser preview ready"; title.textContent = "A tiny taste of the idea.";
    plan.textContent = "This is a handcrafted browser example, not an AI-generated PS5 app.";
    result.hidden = false; busy = false; approve.textContent = "Replay demo ↻"; approve.disabled = false;
    choices.forEach(choice => { choice.disabled = false; });
    if (selected === "counter") {
      let count = 0; const output = document.createElement("output"); output.textContent = "0"; output.setAttribute("aria-label", "Counter value");
      result.append(output, button("+1", () => output.textContent = String(++count)), button("Zero", () => {count = 0; output.textContent = "0";}));
    } else if (selected === "timer") {
      let remaining = 25; const output = document.createElement("output"); output.textContent = "00:25"; output.setAttribute("aria-label", "Demo countdown");
      const start = button("Start", () => {
        if (interval) { clearInterval(interval); interval = null; start.textContent = "Resume"; return; }
        if (!remaining) remaining = 25;
        start.textContent = "Pause";
        interval = setInterval(() => { remaining--; output.textContent = `00:${String(remaining).padStart(2, "0")}`; if (!remaining) { clearInterval(interval); interval = null; start.textContent = "Again"; } }, 1000);
      });
      result.append(output, start);
    } else {
      const grid = document.createElement("div"); grid.className = "pixel-grid"; grid.setAttribute("aria-label", "Six by four pixel canvas");
      for (let i = 0; i < 24; i++) {
        const pixel = button("", () => pixel.setAttribute("aria-pressed", String(pixel.getAttribute("aria-pressed") !== "true")));
        pixel.setAttribute("aria-label", `Pixel ${i + 1}`); pixel.setAttribute("aria-pressed", "false"); grid.append(pixel);
      }
      result.append(grid);
    }
  }
  choices.forEach(choice => choice.addEventListener("click", () => { selected = choice.dataset.idea; reset(); }));
  document.querySelector("#demo-reset").addEventListener("click", reset);
  approve.addEventListener("click", () => {
    if (busy) return; reset(); busy = true;
    approve.disabled = true; approve.textContent = "Playing the walkthrough…";
    choices.forEach(choice => { choice.disabled = true; });
    stage.textContent = "01 / You approved this demo plan";
    buildTimers.push(setTimeout(() => { stage.textContent = "02 / A relay would compile the app"; }, 650));
    buildTimers.push(setTimeout(() => { stage.textContent = "03 / The console would verify the package"; }, 1400));
    buildTimers.push(setTimeout(showResult, 2200));
  });
  const tabs = [...document.querySelectorAll('[role="tab"]')];
  function selectTab(tab) {
    tabs.forEach(item => { const active = item === tab; item.setAttribute("aria-selected", String(active)); item.tabIndex = active ? 0 : -1; document.getElementById(item.getAttribute("aria-controls")).hidden = !active; });
  }
  tabs.forEach((tab, index) => {
    tab.addEventListener("click", () => selectTab(tab));
    tab.addEventListener("keydown", event => {
      let next;
      if (event.key === "ArrowRight") next = (index + 1) % tabs.length;
      if (event.key === "ArrowLeft") next = (index + tabs.length - 1) % tabs.length;
      if (event.key === "Home") next = 0;
      if (event.key === "End") next = tabs.length - 1;
      if (next !== undefined) { event.preventDefault(); selectTab(tabs[next]); tabs[next].focus(); }
    });
  });
  window.addEventListener("pagehide", cleanup);
  document.documentElement.dataset.enhanced = "true";
})();
