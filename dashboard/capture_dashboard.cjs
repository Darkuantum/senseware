const { chromium } = require('/home/drk/.npm/_npx/e41f203b7505f1fb/node_modules/playwright');
const path = require('path');

(async () => {
  const browser = await chromium.launch({
    headless: true,
    args: ['--no-sandbox', '--disable-setuid-sandbox', '--disable-gpu'],
  });

  const page = await browser.newPage({
    viewport: { width: 1920, height: 1080 },
    deviceScaleFactor: 1,
  });

  await page.goto('http://localhost:5173/dashboard', { waitUntil: 'networkidle' });
  await page.waitForTimeout(2000);

  const outputPath = path.join(
    __dirname,
    'public',
    'images',
    'dashboard',
    'dashboard_screenshot.jpg'
  );

  await page.screenshot({
    path: outputPath,
    type: 'jpeg',
    quality: 90,
    fullPage: false,
  });

  console.log(`Screenshot saved to ${outputPath}`);
  await browser.close();
})();
