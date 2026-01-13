async function fetchData(path) {
    try {
        const response = await fetch('https://raw.githubusercontent.com/MitchNine/build_dot_c/refs/heads/master/' + path);
        if (!response.ok) throw new Error('Network response was not ok');
        const data = await response.text();
        return data;
    } catch (err) {
        console.error(err);
        return null;
    }
}

function highlightCode(code) {
    const highlighted = hljs.highlight(code, { language: 'c' }).value;
    return highlighted;
}

function convertEmojiShortcodes(text) {
    const emojiMap = {
        ':white_check_mark:': '✅',
        ':x:': '❌',
        ':warning:': '⚠️',
        ':info:': 'ℹ️',
        ':rocket:': '🚀',
        ':bug:': '🐛',
        ':tada:': '🎉',
        ':lock:': '🔒',
        ':unlock:': '🔓',
        ':fire:': '🔥',
        ':star:': '⭐',
        ':checkmark:': '✔️',
        ':heavy_check_mark:': '✔️',
        ':cross_mark:': '❌',
    };
    
    let result = text;
    Object.entries(emojiMap).forEach(([shortcode, emoji]) => {
        result = result.replace(new RegExp(shortcode.replace(/:/g, '\\:'), 'g'), emoji);
    });
    return result;
}

async function loadRemoteFile(filename, elementId, language = null) {
    try {
        const response = await fetch(`https://raw.githubusercontent.com/MitchNine/build_dot_c/refs/heads/master/${filename}`);
        if (!response.ok) throw new Error('Network response was not ok');
        const data = await response.text();
        const element = document.getElementById(elementId);
        
        if (language === 'c') {
            // Code file: highlight with hljs in pre/code
            const highlighted = hljs.highlight(data, { language }).value;
            element.innerHTML = `<pre><code class="hljs">${highlighted}</code></pre>`;
        } else if (language === 'markdown') {
            // Markdown file: use md-block for rendering
            const convertedData = convertEmojiShortcodes(data);
            const mdBlock = document.createElement('md-block');
            mdBlock.textContent = convertedData;
            element.innerHTML = '';
            element.appendChild(mdBlock);
        } else {
            element.innerText = data;
        }
    } catch (err) {
        document.getElementById(elementId).innerText = `// Unable to load remote file: ${err.message}`;
        console.error(err);
    }
}

async function loadBuildDotC() {
    await loadRemoteFile('build.c', 'build_dot_c', 'c');
}

async function loadChangelog() {
    await loadRemoteFile('CHANGELOG.md', 'changelog_content', 'markdown');
}

async function loadLicense() {
    await loadRemoteFile('LICENCE.md', 'license_content', 'markdown');
}

async function loadSecurity() {
    await loadRemoteFile('SECURITY.md', 'security_content', 'markdown');
}

export { loadBuildDotC, loadChangelog, loadLicense, loadSecurity };