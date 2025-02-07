import { motion, useMotionValueEvent, useScroll } from "motion/react";
import { useEffect, useState } from "react";
import styled from 'styled-components'

const HEIGHT = "h-[70px]"

function NavBarItem(props: { href: string, name: string, selected: boolean }) {
    const { href, name, selected } = props;

    const UnderlinedA = styled.a`
        ${selected && `&:after {
            content: "";
            position: absolute;
            left: 0;
            bottom: 0;
            background-color: #ffb459;
            height: 3px;
            width: calc(100%);
        }`}
    `

    return <li className={`
    `}
    >
        <UnderlinedA href={href} className="relative">
            <div className={`pt-2 pb-3 ${selected ? "text-gray-900" : "text-gray-600"} hover:text-gray-900 font-semibold`}>{name}</div>
        </UnderlinedA>
        {/* <div className="bg-orange-600 h-[3px] absolute bottom-0">.</div> */}
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
            { threshold: 0.90 } // Trigger more frequently
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
            <div className={`
                z-50
                sm:flex
                fixed 
                w-full 
                text-green-950
                bg-white
                border-b-gray-300
                border-b
                pl-8
                hidden

                `}
            >
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