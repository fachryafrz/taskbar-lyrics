document.addEventListener("DOMContentLoaded", async () => {
  const form = document.getElementById("settings-form");
  const fieldsContainer = document.getElementById("settings-fields");
  const applyButton = document.getElementById("apply-button");
  const retryButton = document.getElementById("retry-button");
  const statusMessage = document.getElementById("status-message");
  const errorMessage = document.getElementById("error-message");

  const fields = {
    width: { input: document.getElementById("width"), error: document.getElementById("width-error") },
    height: { input: document.getElementById("height"), error: document.getElementById("height-error") },
    fontSize: { input: document.getElementById("font-size"), error: document.getElementById("font-size-error") },
    lyricOffsetMilliseconds: { input: document.getElementById("lyric-offset"), error: document.getElementById("lyric-offset-error") },
    wrapLongLyrics: { input: document.getElementById("wrap-long-lyrics"), error: null },
    alwaysOnTop: { input: document.getElementById("always-on-top"), error: null },
  };
  const fieldNames = Object.keys(fields);
  const numericBounds = {
    width: { min: 400, max: 2147483647 },
    height: { min: 42, max: 2147483647 },
    fontSize: { min: 1, max: 2147483647 },
    lyricOffsetMilliseconds: { min: -2147483648, max: 2147483647 },
  };

  let savedValues = null;
  let ready = false;
  let saving = false;
  let loadSequence = 0;
  const touchedFields = new Set();

  function setStatus(message) {
    statusMessage.textContent = message;
  }

  function setError(message) {
    errorMessage.textContent = message;
    errorMessage.hidden = !message;
  }

  function errorText(error) {
    if (typeof error === "string") return error;
    if (error instanceof Error) return error.message;
    return String(error);
  }

  function readField(name) {
    const input = fields[name].input;
    if (input.type === "checkbox") return input.checked;
    if (!input.value.trim() || !Number.isInteger(input.valueAsNumber)) return null;
    return input.valueAsNumber;
  }

  function writeField(name, value) {
    const input = fields[name].input;
    if (input.type === "checkbox") {
      input.checked = value;
    } else {
      input.value = String(value);
    }
  }

  function normalizeConfig(config) {
    const normalized = {
      width: config?.width,
      height: config?.height,
      fontSize: config?.fontSize,
      lyricOffsetMilliseconds: config?.lyricOffsetMilliseconds,
      wrapLongLyrics: config?.wrapLongLyrics,
      alwaysOnTop: config?.alwaysOnTop,
    };

    for (const name of fieldNames) {
      const value = normalized[name];
      if (typeof value !== (name === "wrapLongLyrics" || name === "alwaysOnTop" ? "boolean" : "number")) {
        throw new Error("Lyrix returned incomplete settings.");
      }
      if (typeof value === "number" && !Number.isInteger(value)) {
        throw new Error("Lyrix returned invalid numeric settings.");
      }
    }
    return normalized;
  }

  function getFieldError(name) {
    const { input } = fields[name];
    if (input.type === "checkbox") return "";
    const value = readField(name);

    if (!input.value.trim()) return "Enter a value.";
    if (value === null) return "Enter a whole number.";

    const bounds = numericBounds[name];
    if (value < bounds.min) return `Enter ${bounds.min} or more.`;
    if (value > bounds.max) return `Enter ${bounds.max} or less.`;
    return "";
  }

  function updateFieldError(name, shouldShow) {
    const field = fields[name];
    if (!field.error) return false;

    const message = getFieldError(name);
    const visible = shouldShow && Boolean(message);
    field.error.textContent = visible ? message : "";
    field.error.hidden = !visible;
    field.input.setAttribute("aria-invalid", String(visible));
    return Boolean(message);
  }

  function isDirty(name) {
    const value = readField(name);
    return value === null || value !== savedValues[name];
  }

  function collectUpdate(showErrors = false) {
    const update = {};
    let hasInvalidChangedField = false;

    for (const name of fieldNames) {
      const dirty = isDirty(name);
      const shouldShowError = showErrors || touchedFields.has(name);
      const invalid = updateFieldError(name, shouldShowError);
      if (!dirty) continue;
      if (invalid) {
        hasInvalidChangedField = true;
        continue;
      }
      update[name] = readField(name);
    }

    return { update, hasInvalidChangedField };
  }

  function renderFormState() {
    if (!savedValues) {
      applyButton.disabled = true;
      return;
    }
    const { update, hasInvalidChangedField } = collectUpdate();
    applyButton.disabled = !ready || saving || hasInvalidChangedField || Object.keys(update).length === 0;
  }

  function setFormValues(config) {
    savedValues = normalizeConfig(config);
    for (const name of fieldNames) writeField(name, savedValues[name]);
    touchedFields.clear();
    ready = true;
    fieldsContainer.disabled = false;
    setStatus("");
    setError("");
    renderFormState();
  }

  async function loadConfig() {
    const sequence = ++loadSequence;
    ready = false;
    fieldsContainer.disabled = true;
    retryButton.hidden = true;
    applyButton.disabled = true;
    setStatus("Loading settings…");
    setError("");

    try {
      const config = await window.__TAURI__.core.invoke("get_config");
      if (sequence !== loadSequence) return;
      setFormValues(config);
    } catch (error) {
      if (sequence !== loadSequence) return;
      savedValues = null;
      ready = false;
      setStatus("");
      setError(`Settings could not be loaded: ${errorText(error)}`);
      retryButton.hidden = false;
    }
  }

  function mergeExternalConfig(config) {
    if (!ready || !config || typeof config !== "object") return;
    let nextValues;
    try {
      nextValues = normalizeConfig({ ...savedValues, ...config });
    } catch {
      return;
    }

    const dirtyBeforeUpdate = new Set(fieldNames.filter(isDirty));
    savedValues = nextValues;
    for (const name of fieldNames) {
      if (!dirtyBeforeUpdate.has(name)) writeField(name, savedValues[name]);
    }
    renderFormState();
  }

  for (const name of fieldNames) {
    const input = fields[name].input;
    input.addEventListener("input", () => {
      if (touchedFields.has(name)) updateFieldError(name, true);
      setError("");
      setStatus("");
      renderFormState();
    });
    input.addEventListener("change", () => {
      setError("");
      renderFormState();
    });
    input.addEventListener("blur", () => {
      touchedFields.add(name);
      updateFieldError(name, true);
      renderFormState();
    });
  }

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!ready || saving) return;

    const { update, hasInvalidChangedField } = collectUpdate(true);
    if (hasInvalidChangedField || Object.keys(update).length === 0) {
      renderFormState();
      return;
    }

    saving = true;
    fieldsContainer.disabled = true;
    applyButton.textContent = "Applying…";
    setError("");
    setStatus("Applying settings…");
    renderFormState();

    try {
      const config = await window.__TAURI__.core.invoke("apply_settings", { update });
      setFormValues(config);
      setStatus("Settings applied.");
    } catch (error) {
      setError(`Settings could not be saved: ${errorText(error)} Try Apply again.`);
    } finally {
      saving = false;
      fieldsContainer.disabled = !ready;
      applyButton.textContent = "Apply";
      renderFormState();
    }
  });

  retryButton.addEventListener("click", loadConfig);

  const tauri = window.__TAURI__;
  if (!tauri?.core?.invoke) {
    setStatus("");
    setError("Lyrix Settings needs the desktop app runtime.");
    return;
  }

  if (tauri.event?.listen) {
    tauri.event.listen("settings-opened", loadConfig).catch(() => {});
    tauri.event.listen("config-updated", (event) => mergeExternalConfig(event.payload)).catch(() => {});
  }

  await loadConfig();
});
