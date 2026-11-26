import { copyFile, mkdir, readFile, writeFile } from "node:fs/promises";

const release = JSON.parse(await readFile("release.json", "utf8"));
const windows = release.platforms["windows-x64"];
const download = new URL(windows.url);
if (download.origin !== "https://wargamesrealms.ru" ||
    !/^[A-Za-z0-9._-]+\.exe$/.test(windows.fileName) ||
    download.pathname !== "/ageofbattles/" + windows.fileName ||
    !Number.isSafeInteger(windows.size) || windows.size <= 0 ||
    !/^[0-9a-f]{64}$/.test(windows.sha256) ||
    !/^\d{4}\.\d{2}\.\d{2}\.\d+$/.test(release.version) ||
    !/^\d{4}-\d{2}-\d{2}$/.test(release.publishedAt) || release.channel !== "alpha") {
  throw new Error("Invalid Windows release manifest");
}

const commit = process.env.AOB_RELEASE_COMMIT;
if (commit) {
  if (!/^[0-9a-f]{40}$/.test(commit)) throw new Error("Invalid release commit");
  release.source = { commit_sha: commit };
}

const template = await readFile("public/index.html", "utf8");
const fields = {
  DOWNLOAD_PATH: "./" + windows.fileName,
  SIZE_MB: Math.round(windows.size / 1_000_000),
  RELEASE_VERSION: release.version,
  RELEASE_DATE: release.publishedAt,
  DATE_LABEL: new Intl.DateTimeFormat("ru-RU", {
    day: "numeric", month: "long", year: "numeric", timeZone: "UTC"
  }).format(new Date(release.publishedAt)).replace(/\sг\.$/u, ""),
  SHA256: windows.sha256
};
const page = template.replace(/\{\{([A-Z_0-9]+)\}\}/g, (_, name) => {
  if (!(name in fields)) throw new Error("Unknown release field: " + name);
  return fields[name];
});
await mkdir("dist/channels/alpha", { recursive: true });
await Promise.all([
  writeFile("dist/index.html", page),
  copyFile("public/site.css", "dist/site.css"),
  copyFile("public/battlefront.webp", "dist/battlefront.webp"),
  writeFile("dist/channels/alpha/latest.json", JSON.stringify(release, null, 2) + "\n")
]);
