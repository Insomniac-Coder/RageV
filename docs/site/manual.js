
(function () {
	var root = document.documentElement;

	// --- theme ---
	// Dark by default because that is what this engine looks like, but a manual
	// gets read for an hour at a time and that is not everyone's preference.
	var stored = null;
	try { stored = localStorage.getItem("rvdoc-theme"); } catch (e) {}
	if (stored) root.setAttribute("data-theme", stored);

	var themeButton = document.getElementById("theme");
	if (themeButton) {
		themeButton.addEventListener("click", function () {
			var next = root.getAttribute("data-theme") === "light" ? "dark" : "light";
			root.setAttribute("data-theme", next);
			try { localStorage.setItem("rvdoc-theme", next); } catch (e) {}
		});
	}

	var navToggle = document.getElementById("nav-toggle");
	var sidebar = document.querySelector(".sidebar");
	if (navToggle && sidebar) {
		navToggle.addEventListener("click", function () { sidebar.classList.toggle("is-open"); });
	}

	// --- on this page ---
	// Highlights the section actually in view rather than the last one clicked,
	// which is the only version that stays right when someone scrolls.
	var tocLinks = Array.prototype.slice.call(document.querySelectorAll(".toc-link"));
	if (tocLinks.length && "IntersectionObserver" in window) {
		var byId = {};
		tocLinks.forEach(function (link) { byId[link.getAttribute("href").slice(1)] = link; });

		var visible = {};
		var observer = new IntersectionObserver(function (entries) {
			entries.forEach(function (entry) { visible[entry.target.id] = entry.isIntersecting; });
			var current = null;
			Object.keys(byId).forEach(function (id) {
				if (visible[id] && !current) current = id;
			});
			tocLinks.forEach(function (link) { link.classList.remove("is-active"); });
			if (current && byId[current]) byId[current].classList.add("is-active");
		}, { rootMargin: "-72px 0px -70% 0px" });

		Object.keys(byId).forEach(function (id) {
			var heading = document.getElementById(id);
			if (heading) observer.observe(heading);
		});
	}

	// --- search ---
	// The index is a script rather than a fetched file on purpose: fetch() is
	// blocked on file://, and opening the manual from a folder has to work.
	var input = document.getElementById("search-input");
	var results = document.getElementById("search-results");
	var index = window.RVDOC_INDEX || [];
	var prefix = root.getAttribute("data-root") || "";
	var active = -1;

	function close() {
		results.classList.remove("is-open");
		results.innerHTML = "";
		active = -1;
	}

	function run(query) {
		var terms = query.toLowerCase().split(/\s+/).filter(Boolean);
		if (!terms.length) { close(); return; }

		var hits = [];
		for (var i = 0; i < index.length; i++) {
			var entry = index[i];
			var haystack = entry.h.toLowerCase() + " " + entry.p.toLowerCase() + " " + entry.t.toLowerCase();
			var score = 0, matchedAll = true;
			for (var t = 0; t < terms.length; t++) {
				var at = haystack.indexOf(terms[t]);
				if (at < 0) { matchedAll = false; break; }
				// A hit in a heading beats a hit buried in a paragraph.
				score += entry.h.toLowerCase().indexOf(terms[t]) >= 0 ? 10 : 1;
			}
			if (matchedAll) hits.push({ entry: entry, score: score });
		}

		hits.sort(function (a, b) { return b.score - a.score; });
		hits = hits.slice(0, 12);

		if (!hits.length) {
			results.innerHTML = '<p class="result-empty">Nothing matches that.</p>';
			results.classList.add("is-open");
			return;
		}

		results.innerHTML = hits.map(function (hit) {
			var e = hit.entry;
			return '<a class="result" href="' + prefix + e.u + '">' +
				   '<span class="result-title">' + e.h + '</span><br>' +
				   '<span class="result-page">' + e.p + '</span></a>';
		}).join("");
		results.classList.add("is-open");
		active = -1;
	}

	if (input && results) {
		input.addEventListener("input", function () { run(input.value); });
		input.addEventListener("blur", function () { setTimeout(close, 150); });
		input.addEventListener("keydown", function (event) {
			var items = results.querySelectorAll(".result");
			if (event.key === "Escape") { input.blur(); close(); return; }
			if (!items.length) return;
			if (event.key === "ArrowDown" || event.key === "ArrowUp") {
				event.preventDefault();
				active += event.key === "ArrowDown" ? 1 : -1;
				if (active < 0) active = items.length - 1;
				if (active >= items.length) active = 0;
				for (var i = 0; i < items.length; i++) items[i].classList.toggle("is-active", i === active);
				items[active].scrollIntoView({ block: "nearest" });
			} else if (event.key === "Enter" && active >= 0) {
				event.preventDefault();
				window.location.href = items[active].getAttribute("href");
			}
		});

		document.addEventListener("keydown", function (event) {
			if (event.key === "/" && document.activeElement !== input) {
				event.preventDefault();
				input.focus();
			}
		});
	}
})();
