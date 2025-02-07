import { motion, useMotionValueEvent, useScroll } from "motion/react";
import { useEffect, useState } from "react";
import styled from 'styled-components'

const HEIGHT = "h-[70px]"


function NavBarItem(props: { href: string, name: string, selected: boolean }) {
    const { href, name, selected } = props;
    const item = <div className={`pt-2 pb-3 ${selected ? "text-gray-900" : "text-gray-600"} hover:text-gray-900 font-semibold`}>{name}</div>;

    return <li>
        <a href={href} className={`relative ${selected && "highlighted"}`}>{item}</a>
    </li>
}

export default function NavBar() {
    const [lastHash, setLastHash] = useState("");

    const sections = {
        gallery: "Gallery",
        description: "Description",
        specs: "Specs",
        more: "More Info",
    };

    useEffect(() => {

        const observer = new IntersectionObserver(
            (entries) => {
                entries.forEach((entry) => {
                    if (entry.isIntersecting) {
                        setLastHash(entry.target.id);
                    }
                })
            },
            { threshold: 0.50 } // Trigger more frequently
        );

        Object.keys(sections).forEach((id) => {
            const section = document.getElementById(id);
            if (section) observer.observe(section);
        });

        return () => observer.disconnect();

    }, []);

    return (
        <>
            {/* Navbar */}
            <div className="z-50 sm:flex fixed w-full text-green-950 bg-white border-b-gray-300 border-b pl-8 hidden">
                <ul className="flex space-x-24 items-end grow justify-center">
                    {Object.entries(sections).map(([id, name]) => (
                        <NavBarItem key={id} href={`#${id}`} name={name} selected={id == lastHash} />
                    ))}
                </ul>
            </div>
            {/* Gap to take the spot of the navbar */}
            <div className={HEIGHT}></div>
        </>);
}