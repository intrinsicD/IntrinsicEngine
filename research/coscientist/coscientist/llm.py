"""Thin Anthropic API wrapper: per-call model, optional server-side web search, retries,
and tolerant JSON extraction. The agents in agents.py call through this."""
from __future__ import annotations
import re, json, time

try:
    import anthropic
except ImportError:
    anthropic = None


class LLM:
    def __init__(self):
        self.client = anthropic.Anthropic() if anthropic else None

    def available(self) -> bool:
        return self.client is not None

    def call(self, model: str, system: str, user: str,
             max_tokens: int = 4000, web_search: bool = False, retries: int = 3) -> str:
        if not self.client:
            raise RuntimeError("anthropic SDK not installed / no API key; run with offline=True")
        tools = [{"type": "web_search_20250305", "name": "web_search", "max_uses": 5}] \
            if web_search else None
        last = None
        for attempt in range(retries):
            try:
                kw = dict(model=model, max_tokens=max_tokens, system=system,
                          messages=[{"role": "user", "content": user}])
                if tools:
                    kw["tools"] = tools
                r = self.client.messages.create(**kw)
                return "".join(b.text for b in r.content
                               if getattr(b, "type", None) == "text").strip()
            except Exception as e:                       # noqa: BLE001
                last = e
                time.sleep(2 * (attempt + 1))
        raise last

    @staticmethod
    def extract_json(text: str):
        """Strip code fences and parse the first JSON object/array found."""
        t = re.sub(r"```(?:json)?", "", text).strip()
        m = re.search(r"(\{.*\}|\[.*\])", t, re.S)
        if not m:
            return None
        try:
            return json.loads(m.group(1))
        except json.JSONDecodeError:
            return None
