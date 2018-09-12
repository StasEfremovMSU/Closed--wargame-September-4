using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class generalStat  {
    /// <summary>
    /// 1 - три вдоль стоящих части
    /// 2 - три вдоль стоящих части и резерв
    /// </summary>
    public int position_type;

    public int command;

    public List<partStat> parts;
    /// <summary>
    /// Расставить точки - границы 
    /// </summary>
    //public void makeGroupPoints();

   

    public void makeGroupPoints()
    {
        float temp = map.xsize / 3;
        float tempy = map.ysize / 8 * 2;

        switch (position_type)
        {
            case (1):
                // Случай где три части в ряд
                /// Пока просто треть карты у нас занята, не касается численности
                ///  По оси y все рассматриваем как 1/8
                for (int i = 0; i < parts.Count; i++)
                {
                    parts[i].points[0].x = temp + temp / 3 * i;
                    parts[i].points[0].y = tempy;
                    parts[i].points[1].x = temp + temp / 3 + temp / 3 * i;
                    parts[i].points[1].y = tempy;
                    parts[i].points[2].x = temp + temp / 3 + temp / 3 * i;
                    parts[i].points[2].y = tempy/2;
                    parts[i].points[3].x = temp + temp / 3 * i;
                    parts[i].points[3].y = tempy/2;
                }
                    break;

            case (2):
                    // Случай где три части в ряд, а сзади резерв
                    /// Пока просто треть карты у нас занята, не касается численности
                    ///  По оси y все рассматриваем как 1/8
                    for (int i = 0; i < parts.Count - 1; i++)
                    {
                        parts[i].points[0].x = temp + temp / 3 * i;
                        parts[i].points[0].y = tempy;
                        parts[i].points[1].x = temp + temp / 3 + temp / 3 * i;
                        parts[i].points[1].y = tempy;
                        parts[i].points[2].x = temp + temp / 3 + temp / 3 * i;
                        parts[i].points[2].y = tempy / 2;
                        parts[i].points[3].x = temp + temp / 3 * i;
                        parts[i].points[3].y = tempy / 2;
                    }

                        parts[4].points[0].x = temp + temp / 3;
                        parts[4].points[0].y = tempy/2;
                        parts[4].points[1].x = temp + temp / 3 + temp / 3;
                        parts[4].points[1].y = tempy/2;
                        parts[4].points[2].x = temp + temp / 3 + temp / 3;
                        parts[4].points[2].y = 0;
                        parts[4].points[3].x = temp + temp / 3;
                        parts[4].points[3].y = 0;
                    break;

        }
        /// В таком виде справедливо только для первой команды
        

        if (command != 0)
        {
            for (int t = 0; t < parts.Count; t++)
            {
                parts[t].points[0].y = parts[t].points[0].y * (-1) + map.ysize;
                parts[t].points[1].y = parts[t].points[1].y * (-1) + map.ysize;
                parts[t].points[2].y = parts[t].points[2].y * (-1) + map.ysize;
                parts[t].points[3].y = parts[t].points[3].y * (-1) + map.ysize;
            }
        }


    }

	
}

struct mypart
{
    /// <summary>
    ///  1 - авангард
    ///  2 - 3 - 4 правый, центр, левый
    ///  5 -- резерв
    /// </summary>
    public int type;
    public int number;
}
