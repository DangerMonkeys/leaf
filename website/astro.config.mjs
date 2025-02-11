// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

import tailwindcss from '@tailwindcss/vite';

import react from '@astrojs/react';

import icon from 'astro-icon';

import fs from "fs";
import path from "path";

// @ts-ignore
function getMarkdownItems(dir) {
    let results = [];
    let dirMap = {};

    // @ts-ignore
    function readDir(currentDir) {
        const files = fs.readdirSync(currentDir);
        let hasIndex = false;
        let items = [];

        for (const file of files) {
            const fullPath = path.join(currentDir, file);
            const stat = fs.statSync(fullPath);

            if (stat.isDirectory()) {
                readDir(fullPath);
            } else if (file.endsWith(".md")) {
                let relativePath = fullPath.replace("../docs/", "").replace(".md", "");
                if (file === "index.md") {
                    hasIndex = true;
                    relativePath = relativePath.replace(/\/index$/, "");
                }
                items.push(relativePath);
            }
        }

        if (items.length > 0) {
            if (hasIndex && items.length > 1) {
                // @ts-ignore
                dirMap[currentDir] = items;
            } else {
                results.push(...items);
            }
        }
    }

    readDir(dir);

    for (const [key, value] of Object.entries(dirMap)) {
        results.push({
            label: key.split("/").at(-1),
            items: value,
        });
    }

    return results;
}

// https://astro.build/config
export default defineConfig({
    integrations: [starlight({
        title: 'Leaf',
        social: {
            youtube: 'https://www.youtube.com/channel/UCbwWXjxFitbefeKqzAKa94Q',
            instagram: 'https://www.instagram.com/leaf.vario',
            github: 'https://github.com/DangerMonkeys/leaf',
            discord: 'https://discord.com/channels/1325634753663209472/1325634753663209476',
        },
        sidebar: [
            {
                label: 'User Guides',
                autogenerate: { directory: 'user-guides' },
                // items: [
                //     // Each item here is one entry in the navigation menu.
                //     { label: 'Example Guide', slug: 'guides/example' },
                // ],
            },
            {
                label: 'Developer Reference',
                // Use a custom ordering function to allow index files to be in their own directory                
                // @ts-ignore
                items: getMarkdownItems("../docs/dev-references"),
                // autogenerate: { directory: 'dev-references' },
            },
        ],
    }), react(), icon({
        include: {
            "game-icons": [
                'bookmarklet',  // Attribution Required
            ],
            "simple-icons": [
                "github",
                "opensourcehardware",
                "bluetooth",
            ],
            "material-symbols": [
                "bluetooth",
                "wifi-rounded",
                "satellite-alt",
            ]
        },
    })],

    vite: {
        plugins: [tailwindcss()],
        server: {
            allowedHosts: ["sob-desktop.tail2c07b.ts.net", "localhost"]
        }
    },
});