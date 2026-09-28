"""Drive coddy.tech's ASCII art generator and capture the block whole.

Image mode, Shaded style, Color on, Invert off, at the width given.
Every character and the colour the page computed for it comes back, so
the result can be redone rather than remembered.
"""
import asyncio, json, os, sys
from playwright.async_api import async_playwright

SPKI = "KnP1OnzHv/y42eRQmbGwoYTHcSJF448m6CU5mdngwKk="
CHROME = "/opt/pw-browsers/chromium-1194/chrome-linux/chrome"
URL = "https://coddy.tech/tools/ascii-art-generator"

async def grab(pg, image, width):
    await pg.evaluate("""() => {
        const b = [...document.querySelectorAll('button')].find(x => x.innerText.trim()==='Image');
        if (b) b.click(); }""")
    await pg.wait_for_timeout(800)
    await pg.set_input_files("input.AsciiArt_hiddenFile__tI1NC", image)
    await pg.wait_for_timeout(2500)

    # Style: Shaded (the gradient set, not the edge detector).
    await pg.evaluate("""() => {
        const b = [...document.querySelectorAll('button')].find(x => x.innerText.trim()==='Shaded');
        if (b) b.click(); }""")
    # Color on, Invert off.
    await pg.evaluate("""() => {
        const c = [...document.querySelectorAll('input[type=checkbox]')];
        if (c[0] && !c[0].checked) c[0].click();
        if (c[1] &&  c[1].checked) c[1].click(); }""")
    await pg.wait_for_timeout(600)

    # The slider is a controlled React input: set the value through the
    # native setter and dispatch, or the component never hears about it.
    await pg.evaluate("""(w) => {
        const s = document.querySelector('input.AsciiArt_slider__5IYf8');
        const set = Object.getOwnPropertyDescriptor(
            window.HTMLInputElement.prototype, 'value').set;
        set.call(s, String(w));
        s.dispatchEvent(new Event('input', {bubbles:true}));
        s.dispatchEvent(new Event('change', {bubbles:true}));
    }""", width)
    await pg.wait_for_timeout(2500)

    return await pg.evaluate("""() => {
        const pre = document.querySelector('pre[class*=asciiBlock]');
        if (!pre) return null;
        const rows = [];
        // The block is spans; each span carries its own colour. Walk the
        // DOM rather than reading innerText, or the colours are lost.
        let line = {text: '', colours: []};
        const walk = (n) => {
          for (const k of n.childNodes) {
            if (k.nodeType === 3) {
              for (const ch of k.nodeValue) {
                if (ch === '\\n') { rows.push(line); line = {text:'', colours:[]}; }
                else { line.text += ch;
                       line.colours.push(getComputedStyle(k.parentElement).color); }
              }
            } else if (k.tagName === 'BR') {
              rows.push(line); line = {text:'', colours:[]};
            } else walk(k);
          }
        };
        walk(pre);
        if (line.text.length) rows.push(line);
        return {settings: {
                  width: document.querySelector('input.AsciiArt_slider__5IYf8').value,
                  colour: document.querySelectorAll('input[type=checkbox]')[0].checked,
                  invert: document.querySelectorAll('input[type=checkbox]')[1].checked },
                rows};
    }""")

async def main():
    image, width, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    async with async_playwright() as pw:
        b = await pw.chromium.launch(
            executable_path=CHROME if os.path.exists(CHROME) else None,
            args=["--ignore-certificate-errors-spki-list=" + SPKI])
        pg = await b.new_page()
        await pg.goto(URL, wait_until="networkidle", timeout=90000)
        await pg.wait_for_timeout(2000)
        got = await grab(pg, image, width)
        await b.close()
    if got is None:
        sys.exit("no ascii block on the page")
    json.dump(got, open(out, "w"), indent=1)
    print("settings:", got["settings"])
    print("rows:", len(got["rows"]), "widest:", max(len(r["text"]) for r in got["rows"]))
    for r in got["rows"]:
        print("|" + r["text"].rstrip())

asyncio.run(main())
