// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

import tailwindcss from '@tailwindcss/vite';

import react from '@astrojs/react';

import icon from 'astro-icon';

// https://astro.build/config
export default defineConfig({
    base: 'leaf',
    integrations: [starlight({
        title: 'Leaf',
        social: {
            github: 'https://github.com/DangerMonkeys/leaf',
        },
        sidebar: [
            {
                label: 'Guides',
                autogenerate: { directory: 'guides' },
                // items: [
                //     // Each item here is one entry in the navigation menu.
                //     { label: 'Example Guide', slug: 'guides/example' },
                // ],
            },
            {
                label: 'Reference',
                autogenerate: { directory: 'reference' },
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